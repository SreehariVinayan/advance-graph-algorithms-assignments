/* ===================== cycle_canceling.c ===================== */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

#define MAX_LINE_SIZE 1024
#define INF64 INT64_MAX

/*
 * Cycle-Canceling minimum-cost flow solver.
 *
 * Input format (DIMACS-like):
 *   p min <nodes> <arcs>
 *   n <node> <supply>
 *   a <from> <to> <lower> <upper> <cost>
 *
 * Positive node supply means net flow leaves the node.
 * Negative supply means net flow enters the node.
 *
 * Implementation strategy:
 *   1. Build a residual graph using forward/reverse edge pairs.
 *   2. Find any feasible flow with a super-source/super-sink max-flow phase.
 *   3. Repeatedly find a negative residual cycle and saturate its bottleneck.
 *   4. Stop when no negative cycle remains; this is the optimal min-cost flow.
 *
 * Optimisations:
 *   - adjacency lists with contiguous edge storage
 *   - reverse-edge indices for O(1) residual updates
 *   - 64-bit capacities, costs and distances
 *   - Dinic-style feasibility phase with current-arc optimisation
 *   - queue-based Bellman-Ford (SPFA) with Small Label First (SLF) deque
 *   - WARM-STARTED label-correcting search: distance labels, queue and
 *     work arrays persist across cancellations (no O(n) re-initialisation
 *     and no restart from scratch after each cycle)
 *   - negative cycles found by periodic predecessor-graph cycle detection
 *     (amortised O(1) per relaxation) instead of path-length counting
 *   - after a cancellation only the cycle nodes are re-queued and their
 *     predecessor pointers reset
 *   - augment an entire detected cycle by its bottleneck capacity
 */

struct Edge
{
    int32_t to_node;
    int32_t reverse_index;
    int64_t residual_capacity;
    int64_t cost;
};

struct Node
{
    struct Edge *edges;
    int32_t edge_count;
    int32_t edge_capacity;
};

struct OriginalArc
{
    int32_t from;
    int32_t edge_index;
    int64_t lower;
    int64_t cost;
};

struct Graph
{
    int32_t node_count; /* original nodes */
    int32_t original_edge_count;
    int32_t total_node_count; /* original + super source/sink */
    int32_t source;           /* super source */
    int32_t sink;             /* super sink */

    struct Node *nodes;
    int64_t *balance;
    struct OriginalArc *original_arcs;

    /* Benchmark / research statistics. */
    int64_t feasibility_bfs_phases;
    int64_t feasibility_augmentations;
    int64_t feasibility_edge_scans;

    int64_t negative_cycle_searches;
    int64_t negative_cycle_relaxations;
    int64_t negative_cycle_edge_scans;
    int64_t cycle_cancellations;
    int64_t cycle_edge_augmentations;
};

struct CycleEdge
{
    int32_t from;
    int32_t edge_index;
};

static void free_graph(struct Graph *G);

static void allocation_failed(void)
{
    fprintf(stderr, "Memory allocation failed.\n");
    exit(EXIT_FAILURE);
}

static int ensure_edge_capacity(struct Node *node, int32_t required)
{
    if (required <= node->edge_capacity)
        return 1;

    int32_t new_capacity = node->edge_capacity == 0 ? 4 : node->edge_capacity;
    while (new_capacity < required)
    {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }

    struct Edge *tmp = realloc(
        node->edges,
        (size_t)new_capacity * sizeof(*tmp));

    if (tmp == NULL)
        return 0;

    node->edges = tmp;
    node->edge_capacity = new_capacity;
    return 1;
}

/* Adds a residual forward/reverse pair and returns the forward index. */
static int32_t add_edge(
    struct Graph *G,
    int32_t from,
    int32_t to,
    int64_t capacity,
    int64_t cost)
{
    struct Node *U = &G->nodes[from];
    struct Node *V = &G->nodes[to];

    int32_t forward_index;
    int32_t reverse_index;

    if (from == to)
    {
        /* Self-loops need two distinct positions in the same array. */
        if (!ensure_edge_capacity(U, U->edge_count + 2))
            allocation_failed();

        forward_index = U->edge_count;
        reverse_index = U->edge_count + 1;

        U->edges[forward_index] = (struct Edge){
            .to_node = to,
            .reverse_index = reverse_index,
            .residual_capacity = capacity,
            .cost = cost};

        U->edges[reverse_index] = (struct Edge){
            .to_node = from,
            .reverse_index = forward_index,
            .residual_capacity = 0,
            .cost = -cost};

        U->edge_count += 2;
        return forward_index;
    }

    if (!ensure_edge_capacity(U, U->edge_count + 1) ||
        !ensure_edge_capacity(V, V->edge_count + 1))
    {
        allocation_failed();
    }

    forward_index = U->edge_count;
    reverse_index = V->edge_count;

    U->edges[forward_index] = (struct Edge){
        .to_node = to,
        .reverse_index = reverse_index,
        .residual_capacity = capacity,
        .cost = cost};

    V->edges[reverse_index] = (struct Edge){
        .to_node = from,
        .reverse_index = forward_index,
        .residual_capacity = 0,
        .cost = -cost};

    U->edge_count++;
    V->edge_count++;
    return forward_index;
}

static struct Graph *build_graph_from_dataset(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Cannot open dataset '%s': %s\n", filename, strerror(errno));
        return NULL;
    }

    int32_t node_count = -1;
    int32_t edge_count = -1;

    char line[MAX_LINE_SIZE];

    /* The DIMACS header precedes node/arc records. */
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *token = strtok(line, " \t\r\n");
        if (token == NULL || token[0] == 'c')
            continue;

        if (strcmp(token, "p") == 0)
        {
            char *type = strtok(NULL, " \t\r\n");
            char *n_token = strtok(NULL, " \t\r\n");
            char *m_token = strtok(NULL, " \t\r\n");

            if (type == NULL || n_token == NULL || m_token == NULL ||
                strcmp(type, "min") != 0)
            {
                fprintf(stderr, "Invalid DIMACS problem line.\n");
                fclose(fp);
                return NULL;
            }

            char *endptr = NULL;
            long n_value = strtol(n_token, &endptr, 10);
            if (*n_token == '\0' || *endptr != '\0' ||
                n_value <= 0 || n_value > INT32_MAX)
            {
                fprintf(stderr, "Invalid node count.\n");
                fclose(fp);
                return NULL;
            }

            endptr = NULL;
            long m_value = strtol(m_token, &endptr, 10);
            if (*m_token == '\0' || *endptr != '\0' ||
                m_value < 0 || m_value > INT32_MAX)
            {
                fprintf(stderr, "Invalid edge count.\n");
                fclose(fp);
                return NULL;
            }

            node_count = (int32_t)n_value;
            edge_count = (int32_t)m_value;
            break;
        }
    }

    if (node_count <= 0 || edge_count < 0)
    {
        fprintf(stderr, "Dataset does not contain a valid 'p min' line.\n");
        fclose(fp);
        return NULL;
    }

    struct Graph *G = calloc(1, sizeof(*G));
    if (G == NULL)
        allocation_failed();

    G->node_count = node_count;
    G->original_edge_count = edge_count;
    G->total_node_count = node_count + 2;
    G->source = node_count;
    G->sink = node_count + 1;

    G->nodes = calloc((size_t)G->total_node_count, sizeof(*G->nodes));
    G->balance = calloc((size_t)node_count, sizeof(*G->balance));
    G->original_arcs = calloc((size_t)(edge_count > 0 ? edge_count : 1), sizeof(*G->original_arcs));

    if (G->nodes == NULL || G->balance == NULL || G->original_arcs == NULL)
        allocation_failed();

    int32_t parsed_arcs = 0;

    /* Re-read from the beginning so comments/records are handled uniformly. */
    rewind(fp);

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *token = strtok(line, " \t\r\n");
        if (token == NULL || token[0] == 'c')
            continue;

        if (strcmp(token, "p") == 0)
        {
            continue;
        }

        if (strcmp(token, "n") == 0)
        {
            char *id_token = strtok(NULL, " \t\r\n");
            char *supply_token = strtok(NULL, " \t\r\n");

            if (id_token == NULL || supply_token == NULL)
            {
                fprintf(stderr, "Malformed node line.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            char *endptr = NULL;
            long node_id = strtol(id_token, &endptr, 10);
            if (*id_token == '\0' || *endptr != '\0' ||
                node_id < 1 || node_id > node_count)
            {
                fprintf(stderr, "Node id out of range: %s\n", id_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            endptr = NULL;
            long long supply = strtoll(supply_token, &endptr, 10);
            if (*supply_token == '\0' || *endptr != '\0')
            {
                fprintf(stderr, "Invalid node supply: %s\n", supply_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            G->balance[(int32_t)node_id - 1] = (int64_t)supply;
        }
        else if (strcmp(token, "a") == 0)
        {
            char *u_token = strtok(NULL, " \t\r\n");
            char *v_token = strtok(NULL, " \t\r\n");
            char *lower_token = strtok(NULL, " \t\r\n");
            char *upper_token = strtok(NULL, " \t\r\n");
            char *cost_token = strtok(NULL, " \t\r\n");

            if (u_token == NULL || v_token == NULL ||
                lower_token == NULL || upper_token == NULL ||
                cost_token == NULL)
            {
                fprintf(stderr, "Malformed arc line.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            char *endptr = NULL;
            long u_value = strtol(u_token, &endptr, 10);
            if (*u_token == '\0' || *endptr != '\0' ||
                u_value < 1 || u_value > node_count)
            {
                fprintf(stderr, "Arc source out of range: %s\n", u_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            endptr = NULL;
            long v_value = strtol(v_token, &endptr, 10);
            if (*v_token == '\0' || *endptr != '\0' ||
                v_value < 1 || v_value > node_count)
            {
                fprintf(stderr, "Arc destination out of range: %s\n", v_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            endptr = NULL;
            long long lower = strtoll(lower_token, &endptr, 10);
            if (*lower_token == '\0' || *endptr != '\0' || lower < 0)
            {
                fprintf(stderr, "Invalid lower capacity: %s\n", lower_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            endptr = NULL;
            long long upper = strtoll(upper_token, &endptr, 10);
            if (*upper_token == '\0' || *endptr != '\0' || upper < lower)
            {
                fprintf(stderr, "Invalid upper capacity: %s\n", upper_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            endptr = NULL;
            long long cost = strtoll(cost_token, &endptr, 10);
            if (*cost_token == '\0' || *endptr != '\0')
            {
                fprintf(stderr, "Invalid arc cost: %s\n", cost_token);
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            if (parsed_arcs >= edge_count)
            {
                fprintf(stderr, "Dataset contains more arcs than declared.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            int32_t u = (int32_t)u_value - 1;
            int32_t v = (int32_t)v_value - 1;

            int32_t forward_index = add_edge(
                G,
                u,
                v,
                (int64_t)upper - (int64_t)lower,
                (int64_t)cost);

            G->original_arcs[parsed_arcs] = (struct OriginalArc){
                .from = u,
                .edge_index = forward_index,
                .lower = (int64_t)lower,
                .cost = (int64_t)cost};

            /* Remove the lower-bound flow contribution from the remaining balance. */
            G->balance[u] -= (int64_t)lower;
            G->balance[v] += (int64_t)lower;

            parsed_arcs++;
        }
        else
        {
            fprintf(stderr, "Unknown record type '%s'.\n", token);
            free_graph(G);
            fclose(fp);
            return NULL;
        }
    }

    fclose(fp);

    if (parsed_arcs != edge_count)
    {
        fprintf(stderr,
                "Declared %" PRId32 " arcs but parsed %" PRId32 ".\n",
                edge_count,
                parsed_arcs);
        free_graph(G);
        return NULL;
    }

    int64_t balance_sum = 0;
    for (int32_t i = 0; i < node_count; ++i)
        balance_sum += G->balance[i];

    if (balance_sum != 0)
    {
        fprintf(stderr,
                "Total supply after lower bounds is not zero: %" PRId64 "\n",
                balance_sum);
        free_graph(G);
        return NULL;
    }

    return G;
}

static void free_graph(struct Graph *G)
{
    if (G == NULL)
        return;

    if (G->nodes != NULL)
    {
        for (int32_t i = 0; i < G->total_node_count; ++i)
            free(G->nodes[i].edges);
        free(G->nodes);
    }

    free(G->balance);
    free(G->original_arcs);
    free(G);
}

/* -------------------------------------------------------------
 * Feasibility phase: Dinic max flow ignoring costs.
 * ------------------------------------------------------------- */

static int32_t *g_feas_level;
static int32_t *g_feas_current;
static int32_t *g_feas_queue;

static int32_t feasibility_bfs(struct Graph *G)
{
    const int32_t n = G->total_node_count;
    const int32_t s = G->source;
    const int32_t t = G->sink;

    for (int32_t i = 0; i < n; ++i)
        g_feas_level[i] = -1;

    int32_t *queue = g_feas_queue;

    int32_t front = 0;
    int32_t rear = 0;
    g_feas_level[s] = 0;
    queue[rear++] = s;

    while (front < rear)
    {
        int32_t u = queue[front++];
        struct Node *node = &G->nodes[u];
        int32_t next_level = g_feas_level[u] + 1;

        for (int32_t i = 0; i < node->edge_count; ++i)
        {
            G->feasibility_edge_scans++;
            struct Edge *e = &node->edges[i];

            if (e->residual_capacity <= 0)
                continue;

            int32_t v = e->to_node;
            if (g_feas_level[v] != -1)
                continue;

            g_feas_level[v] = next_level;
            if (v == t)
            {
                G->feasibility_bfs_phases++;
                return 1;
            }
            queue[rear++] = v;
        }
    }

    G->feasibility_bfs_phases++;
    return 0;
}

static int64_t feasibility_dfs(
    struct Graph *G,
    int32_t u,
    int32_t t,
    int64_t pushed)
{
    if (u == t)
        return pushed;

    struct Node *node = &G->nodes[u];

    while (g_feas_current[u] < node->edge_count)
    {
        int32_t edge_index = g_feas_current[u];
        struct Edge *e = &node->edges[edge_index];

        G->feasibility_edge_scans++;

        if (e->residual_capacity <= 0 ||
            g_feas_level[e->to_node] != g_feas_level[u] + 1)
        {
            g_feas_current[u]++;
            continue;
        }

        int64_t allowed = pushed < e->residual_capacity
                              ? pushed
                              : e->residual_capacity;

        int64_t flow = feasibility_dfs(G, e->to_node, t, allowed);

        if (flow > 0)
        {
            e->residual_capacity -= flow;
            G->nodes[e->to_node]
                .edges[e->reverse_index]
                .residual_capacity += flow;
            return flow;
        }

        g_feas_current[u]++;
    }

    return 0;
}

static int make_initial_feasible_flow(struct Graph *G)
{
    int64_t required = 0;

    for (int32_t i = 0; i < G->node_count; ++i)
    {
        if (G->balance[i] > 0)
        {
            add_edge(G, G->source, i, G->balance[i], 0);
            required += G->balance[i];
        }
        else if (G->balance[i] < 0)
        {
            add_edge(G, i, G->sink, -G->balance[i], 0);
        }
    }

    g_feas_level = malloc(
        (size_t)G->total_node_count * sizeof(*g_feas_level));
    g_feas_current = malloc(
        (size_t)G->total_node_count * sizeof(*g_feas_current));
    g_feas_queue = malloc(
        (size_t)G->total_node_count * sizeof(*g_feas_queue));

    if (g_feas_level == NULL || g_feas_current == NULL || g_feas_queue == NULL)
        allocation_failed();

    int64_t flow = 0;

    while (flow < required && feasibility_bfs(G))
    {
        memset(
            g_feas_current,
            0,
            (size_t)G->total_node_count * sizeof(*g_feas_current));

        while (flow < required)
        {
            int64_t pushed = feasibility_dfs(
                G,
                G->source,
                G->sink,
                required - flow);

            if (pushed <= 0)
                break;

            flow += pushed;
            G->feasibility_augmentations++;
        }
    }

    free(g_feas_level);
    free(g_feas_current);
    free(g_feas_queue);
    g_feas_level = NULL;
    g_feas_current = NULL;
    g_feas_queue = NULL;

    if (flow != required)
    {
        fprintf(stderr,
                "No feasible flow exists: required=%" PRId64 " obtained=%" PRId64 "\n",
                required,
                flow);
        return 0;
    }

    /* Disable every artificial super-source/super-sink residual edge. */
    for (int32_t u = 0; u < G->total_node_count; ++u)
    {
        if (u >= G->node_count)
        {
            for (int32_t j = 0; j < G->nodes[u].edge_count; ++j)
                G->nodes[u].edges[j].residual_capacity = 0;
            continue;
        }

        for (int32_t j = 0; j < G->nodes[u].edge_count; ++j)
        {
            struct Edge *e = &G->nodes[u].edges[j];
            if (e->to_node >= G->node_count)
                e->residual_capacity = 0;
        }
    }

    return 1;
}

/* -------------------------------------------------------------
 * Negative cycle detection (warm-started label-correcting).
 * ------------------------------------------------------------- */

struct Deque
{
    int32_t *data;
    int32_t capacity;
    int32_t head;
    int32_t size;
};

static void deque_init(struct Deque *Q, int32_t capacity)
{
    Q->data = malloc((size_t)capacity * sizeof(*Q->data));
    if (Q->data == NULL)
        allocation_failed();
    Q->capacity = capacity;
    Q->head = 0;
    Q->size = 0;
}

static void deque_free(struct Deque *Q)
{
    free(Q->data);
    Q->data = NULL;
}

static int deque_empty(const struct Deque *Q)
{
    return Q->size == 0;
}

static void deque_push_front(struct Deque *Q, int32_t value)
{
    if (Q->size >= Q->capacity)
        allocation_failed();

    Q->head = Q->head == 0 ? Q->capacity - 1 : Q->head - 1;
    Q->data[Q->head] = value;
    Q->size++;
}

static void deque_push_back(struct Deque *Q, int32_t value)
{
    if (Q->size >= Q->capacity)
        allocation_failed();

    int32_t index = Q->head + Q->size;
    if (index >= Q->capacity)
        index -= Q->capacity;
    Q->data[index] = value;
    Q->size++;
}

static int32_t deque_pop_front(struct Deque *Q)
{
    if (Q->size == 0)
        return -1;

    int32_t value = Q->data[Q->head];
    Q->head++;
    if (Q->head == Q->capacity)
        Q->head = 0;
    Q->size--;
    return value;
}

/*
 * Searches the predecessor graph for a cycle.  Any cycle in the predecessor
 * graph of a label-correcting algorithm is a negative residual cycle.
 * Returns a node on the cycle, or -1.
 */
static int32_t find_parent_cycle(
    int32_t n,
    const int32_t *parent_node,
    int64_t *visit,
    int64_t *stamp_counter)
{
    const int64_t base = *stamp_counter;

    for (int32_t s = 0; s < n; ++s)
    {
        if (visit[s] > base)
            continue;

        int64_t stamp = ++(*stamp_counter);
        int32_t v = s;

        while (v != -1)
        {
            if (visit[v] > base)
            {
                if (visit[v] == stamp)
                    return v;
                break;
            }
            visit[v] = stamp;
            v = parent_node[v];
        }
    }

    return -1;
}

static int64_t compute_total_cost(const struct Graph *G)
{
    int64_t total_cost = 0;

    for (int32_t i = 0; i < G->original_edge_count; ++i)
    {
        const struct OriginalArc *arc = &G->original_arcs[i];
        const struct Edge *forward =
            &G->nodes[arc->from].edges[arc->edge_index];
        const struct Edge *reverse =
            &G->nodes[forward->to_node].edges[forward->reverse_index];

        int64_t flow = arc->lower + reverse->residual_capacity;
        total_cost += flow * arc->cost;
    }

    return total_cost;
}

static int64_t run_cycle_canceling(struct Graph *G)
{
    if (!make_initial_feasible_flow(G))
        return INT64_MIN;

    const int32_t n = G->node_count;

    int64_t *distance = calloc((size_t)n, sizeof(*distance));
    int32_t *parent_node = malloc((size_t)n * sizeof(*parent_node));
    int32_t *parent_edge = malloc((size_t)n * sizeof(*parent_edge));
    uint8_t *in_queue = calloc((size_t)n, sizeof(*in_queue));
    int64_t *visit = calloc((size_t)n, sizeof(*visit));
    struct CycleEdge *cycle_edges = malloc((size_t)n * sizeof(*cycle_edges));
    int32_t *cycle_nodes = malloc((size_t)n * sizeof(*cycle_nodes));

    if (distance == NULL || parent_node == NULL || parent_edge == NULL ||
        in_queue == NULL || visit == NULL || cycle_edges == NULL ||
        cycle_nodes == NULL)
    {
        allocation_failed();
    }

    for (int32_t i = 0; i < n; ++i)
    {
        parent_node[i] = -1;
        parent_edge[i] = -1;
    }

    struct Deque Q;
    deque_init(&Q, n + 1);

    /* Zero labels on every vertex == virtual source with 0-cost arcs. */
    for (int32_t i = 0; i < n; ++i)
    {
        in_queue[i] = 1;
        deque_push_back(&Q, i);
    }

    int64_t total_cost = compute_total_cost(G);
    int64_t stamp_counter = 0;
    int64_t relax_since_check = 0;
    const int64_t check_interval = n > 64 ? n : 64;

    G->negative_cycle_searches++;

    for (;;)
    {
        int32_t cycle_vertex = -1;

        while (!deque_empty(&Q))
        {
            int32_t u = deque_pop_front(&Q);
            in_queue[u] = 0;

            struct Node *node = &G->nodes[u];
            const int64_t du = distance[u];

            for (int32_t i = 0; i < node->edge_count; ++i)
            {
                G->negative_cycle_edge_scans++;

                struct Edge *e = &node->edges[i];
                if (e->residual_capacity <= 0)
                    continue;

                int32_t v = e->to_node;
                if (v >= n)
                    continue; /* artificial super-node edge */

                int64_t candidate = du + e->cost;
                if (candidate >= distance[v])
                    continue;

                distance[v] = candidate;
                parent_node[v] = u;
                parent_edge[v] = i;
                G->negative_cycle_relaxations++;
                relax_since_check++;

                if (!in_queue[v])
                {
                    /* SLF: small labels go to the front. */
                    if (!deque_empty(&Q) &&
                        distance[v] < distance[Q.data[Q.head]])
                        deque_push_front(&Q, v);
                    else
                        deque_push_back(&Q, v);
                    in_queue[v] = 1;
                }
            }

            if (relax_since_check >= check_interval)
            {
                relax_since_check = 0;
                cycle_vertex = find_parent_cycle(
                    n, parent_node, visit, &stamp_counter);
                if (cycle_vertex != -1)
                    break;
            }
        }

        if (cycle_vertex == -1)
        {
            /* Queue drained: confirm there is no cycle left. */
            cycle_vertex = find_parent_cycle(
                n, parent_node, visit, &stamp_counter);
            if (cycle_vertex == -1)
                break;
        }

        /* ---- Extract and cancel the cycle ---- */
        int32_t count = 0;
        int64_t cost_sum = 0;
        int64_t bottleneck = INF64;
        int32_t cur = cycle_vertex;
        int valid = 1;

        do
        {
            int32_t p = parent_node[cur];
            int32_t eidx = parent_edge[cur];

            if (p < 0 || eidx < 0 || count >= n)
            {
                valid = 0;
                break;
            }

            struct Edge *e = &G->nodes[p].edges[eidx];
            if (e->residual_capacity <= 0)
            {
                valid = 0;
                break;
            }

            cycle_edges[count].from = p;
            cycle_edges[count].edge_index = eidx;
            cycle_nodes[count] = cur;
            cost_sum += e->cost;
            if (e->residual_capacity < bottleneck)
                bottleneck = e->residual_capacity;

            count++;
            cur = p;
        } while (cur != cycle_vertex);

        if (!valid || count <= 0 || cost_sum >= 0 || bottleneck <= 0)
        {
            /* Defensive: drop the stale pointers and keep searching. */
            for (int32_t i = 0; i < count; ++i)
            {
                parent_node[cycle_nodes[i]] = -1;
                parent_edge[cycle_nodes[i]] = -1;
            }
            parent_node[cycle_vertex] = -1;
            parent_edge[cycle_vertex] = -1;
            continue;
        }

        for (int32_t i = 0; i < count; ++i)
        {
            int32_t u = cycle_edges[i].from;
            struct Edge *e = &G->nodes[u].edges[cycle_edges[i].edge_index];

            e->residual_capacity -= bottleneck;
            G->nodes[e->to_node]
                .edges[e->reverse_index]
                .residual_capacity += bottleneck;

            G->cycle_edge_augmentations++;
        }

        total_cost += bottleneck * cost_sum;
        G->cycle_cancellations++;

        /* Warm start: reset only cycle predecessors, re-queue cycle nodes. */
        for (int32_t i = 0; i < count; ++i)
        {
            int32_t v = cycle_nodes[i];
            parent_node[v] = -1;
            parent_edge[v] = -1;

            if (!in_queue[v])
            {
                in_queue[v] = 1;
                deque_push_back(&Q, v);
            }
        }
        relax_since_check = 0;
    }

    /* Recompute as an integrity check against incremental cost updates. */
    int64_t verified_cost = compute_total_cost(G);
    if (verified_cost != total_cost)
    {
        fprintf(stderr,
                "Internal cost mismatch: tracked=%" PRId64 " verified=%" PRId64 "\n",
                total_cost,
                verified_cost);
        total_cost = verified_cost;
    }

    deque_free(&Q);
    free(distance);
    free(parent_node);
    free(parent_edge);
    free(in_queue);
    free(visit);
    free(cycle_edges);
    free(cycle_nodes);
    return total_cost;
}

static int64_t elapsed_microseconds(
    const struct timespec *start,
    const struct timespec *end)
{
    return (int64_t)(end->tv_sec - start->tv_sec) * 1000000LL +
           (int64_t)(end->tv_nsec - start->tv_nsec) / 1000LL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <dataset-file>\n", argv[0]);
        return 1;
    }

    struct Graph *G = build_graph_from_dataset(argv[1]);
    if (G == NULL)
        return 1;

    printf("Number of nodes = %" PRId32 "\n", G->node_count);
    printf("Number of edges = %" PRId32 "\n", G->original_edge_count);

    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    int64_t total_cost = run_cycle_canceling(G);

    clock_gettime(CLOCK_MONOTONIC, &end);

    if (total_cost == INT64_MIN)
    {
        free_graph(G);
        return 2;
    }

    int64_t execution_time_us = elapsed_microseconds(&start, &end);

    printf("Minimum cost = %" PRId64 "\n", total_cost);
    printf("Execution time = %" PRId64 " us\n", execution_time_us);
    printf("Feasibility BFS phases = %" PRId64 "\n", G->feasibility_bfs_phases);
    printf("Feasibility augmentations = %" PRId64 "\n", G->feasibility_augmentations);
    printf("Feasibility edge scans = %" PRId64 "\n", G->feasibility_edge_scans);
    printf("Negative-cycle searches = %" PRId64 "\n", G->negative_cycle_searches);
    printf("Negative-cycle relaxations = %" PRId64 "\n", G->negative_cycle_relaxations);
    printf("Negative-cycle edge scans = %" PRId64 "\n", G->negative_cycle_edge_scans);
    printf("Cycle cancellations = %" PRId64 "\n", G->cycle_cancellations);
    printf("Cycle edge augmentations = %" PRId64 "\n", G->cycle_edge_augmentations);

    printf("\nBENCH_RESULT algorithm=Cycle-Canceling "
           "nodes=%" PRId32 " edges=%" PRId32 " "
           "total_cost=%" PRId64 " execution_time_us=%" PRId64 " "
           "feasibility_bfs_phases=%" PRId64 " "
           "feasibility_augmentations=%" PRId64 " "
           "feasibility_edge_scans=%" PRId64 " "
           "negative_cycle_searches=%" PRId64 " "
           "negative_cycle_relaxations=%" PRId64 " "
           "negative_cycle_edge_scans=%" PRId64 " "
           "cycle_cancellations=%" PRId64 " "
           "cycle_edge_augmentations=%" PRId64 "\n",
           G->node_count,
           G->original_edge_count,
           total_cost,
           execution_time_us,
           G->feasibility_bfs_phases,
           G->feasibility_augmentations,
           G->feasibility_edge_scans,
           G->negative_cycle_searches,
           G->negative_cycle_relaxations,
           G->negative_cycle_edge_scans,
           G->cycle_cancellations,
           G->cycle_edge_augmentations);

    free_graph(G);
    return 0;
}