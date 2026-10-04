/* ===================== capacity_scaling.c ===================== */
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
#define INF64 (INT64_MAX / 4)

/*
 * Capacity Scaling minimum-cost flow solver.
 *
 * Input:
 *   p min <nodes> <arcs>
 *   n <node> <supply>
 *   a <from> <to> <lower> <upper> <cost>
 *
 * Positive supply means net flow leaves a node; negative supply means
 * net flow enters a node.
 *
 * The implementation is the capacity-scaling successive-shortest-path
 * algorithm.  In each Delta phase it first saturates Delta-residual arcs
 * having negative reduced cost, then repeatedly routes a large surplus to
 * a large deficit along a shortest path in the Delta-residual network.
 *
 * Optimisations:
 *   - adjacency lists and compact forward/reverse residual pairs
 *   - reverse-edge indices for O(1) residual updates
 *   - lower-bound transformation without an auxiliary max-flow phase
 *   - reduced costs with vertex potentials
 *   - indexed binary heap with decrease-key for Dijkstra
 *   - Delta-residual edge filtering during shortest-path scans
 *   - full bottleneck augmentation instead of one-unit/Delta augmentation
 *   - O(1) surplus/deficit membership updates
 *   - negative reduced-cost arc saturation at the start of each phase
 *   - multi-source Dijkstra with EARLY TERMINATION at the first deficit
 *     node; potentials updated as pi[v] -= min(d[v], d[t]) - d[t]
 *   - per-search resets restricted to touched nodes (no O(n) re-init)
 *   - a failed search ends the phase (no surplus can reach any deficit)
 *   - 64-bit arithmetic throughout
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

    int64_t excess;
    int64_t potential;

    int32_t s_next;
    int32_t s_prev;
    int32_t t_next;
    int32_t t_prev;
    uint8_t in_surplus;
    uint8_t in_deficit;
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
    int32_t node_count;
    int32_t original_edge_count;
    struct Node *nodes;
    int64_t *balance;
    struct OriginalArc *original_arcs;

    int64_t initial_cost;
    int64_t max_residual_capacity;

    int64_t scaling_phases;
    int64_t negative_arc_saturations;
    int64_t negative_arc_edge_scans;
    int64_t shortest_path_searches;
    int64_t shortest_path_successes;
    int64_t shortest_path_failures;
    int64_t shortest_path_edge_scans;
    int64_t dijkstra_relaxations;
    int64_t heap_insert_or_decrease;
    int64_t heap_pops;
    int64_t augmentations;
    int64_t path_edge_augmentations;
};

struct ActiveSets
{
    int32_t surplus_head;
    int32_t deficit_head;
};

struct Heap
{
    int32_t *node;
    int64_t *key;
    int32_t *position;
    int32_t size;
    int32_t capacity;
};

static void allocation_failed(void)
{
    fprintf(stderr, "Memory allocation failed.\n");
    exit(EXIT_FAILURE);
}

static void free_graph(struct Graph *G);

static int64_t parse_int64_token(const char *token, int *ok)
{
    char *endptr = NULL;
    errno = 0;
    long long value = strtoll(token, &endptr, 10);

    if (token == NULL || *token == '\0' || *endptr != '\0' || errno == ERANGE)
    {
        *ok = 0;
        return 0;
    }

    *ok = 1;
    return (int64_t)value;
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
        fprintf(stderr,
                "Cannot open dataset '%s': %s\n",
                filename,
                strerror(errno));
        return NULL;
    }

    int32_t node_count = -1;
    int32_t edge_count = -1;
    char line[MAX_LINE_SIZE];

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

            int ok = 0;
            int64_t n64 = parse_int64_token(n_token, &ok);
            if (!ok || n64 <= 0 || n64 > INT32_MAX)
            {
                fprintf(stderr, "Invalid node count.\n");
                fclose(fp);
                return NULL;
            }

            int64_t m64 = parse_int64_token(m_token, &ok);
            if (!ok || m64 < 0 || m64 > INT32_MAX)
            {
                fprintf(stderr, "Invalid edge count.\n");
                fclose(fp);
                return NULL;
            }

            node_count = (int32_t)n64;
            edge_count = (int32_t)m64;
            break;
        }
    }

    if (node_count <= 0 || edge_count < 0)
    {
        fprintf(stderr, "Dataset does not contain a valid 'p min' line.\n");
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    struct Graph *G = calloc(1, sizeof(*G));
    if (G == NULL)
        allocation_failed();

    G->node_count = node_count;
    G->original_edge_count = edge_count;
    G->nodes = calloc((size_t)node_count, sizeof(*G->nodes));
    G->balance = calloc((size_t)node_count, sizeof(*G->balance));
    G->original_arcs = calloc((size_t)(edge_count > 0 ? edge_count : 1), sizeof(*G->original_arcs));

    if (G->nodes == NULL || G->balance == NULL || G->original_arcs == NULL)
        allocation_failed();

    for (int32_t i = 0; i < node_count; ++i)
    {
        G->nodes[i].s_next = -1;
        G->nodes[i].s_prev = -1;
        G->nodes[i].t_next = -1;
        G->nodes[i].t_prev = -1;
    }

    int32_t parsed_edges = 0;

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *token = strtok(line, " \t\r\n");
        if (token == NULL || token[0] == 'c')
            continue;

        if (strcmp(token, "n") == 0)
        {
            char *id_token = strtok(NULL, " \t\r\n");
            char *supply_token = strtok(NULL, " \t\r\n");
            if (id_token == NULL || supply_token == NULL)
            {
                fprintf(stderr, "Malformed node record.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            int ok_id = 0;
            int ok_supply = 0;
            int64_t id64 = parse_int64_token(id_token, &ok_id);
            int64_t supply = parse_int64_token(supply_token, &ok_supply);
            if (!ok_id || !ok_supply || id64 < 1 || id64 > node_count)
            {
                fprintf(stderr, "Invalid node record.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            G->balance[(int32_t)id64 - 1] = supply;
            continue;
        }

        if (strcmp(token, "a") == 0)
        {
            if (parsed_edges >= edge_count)
            {
                fprintf(stderr, "More arc records than declared in header.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            char *u_token = strtok(NULL, " \t\r\n");
            char *v_token = strtok(NULL, " \t\r\n");
            char *lower_token = strtok(NULL, " \t\r\n");
            char *upper_token = strtok(NULL, " \t\r\n");
            char *cost_token = strtok(NULL, " \t\r\n");

            if (u_token == NULL || v_token == NULL || lower_token == NULL ||
                upper_token == NULL || cost_token == NULL)
            {
                fprintf(stderr, "Malformed arc record.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            int ok_u = 0, ok_v = 0, ok_l = 0, ok_upr = 0, ok_c = 0;
            int64_t u64 = parse_int64_token(u_token, &ok_u);
            int64_t v64 = parse_int64_token(v_token, &ok_v);
            int64_t lower = parse_int64_token(lower_token, &ok_l);
            int64_t upper = parse_int64_token(upper_token, &ok_upr);
            int64_t cost = parse_int64_token(cost_token, &ok_c);

            if (!ok_u || !ok_v || !ok_l || !ok_upr || !ok_c ||
                u64 < 1 || u64 > node_count ||
                v64 < 1 || v64 > node_count ||
                lower < 0 || upper < lower)
            {
                fprintf(stderr, "Invalid arc record.\n");
                free_graph(G);
                fclose(fp);
                return NULL;
            }

            int32_t u = (int32_t)u64 - 1;
            int32_t v = (int32_t)v64 - 1;
            int64_t residual_capacity = upper - lower;

            int32_t edge_index = add_edge(
                G,
                u,
                v,
                residual_capacity,
                cost);

            G->original_arcs[parsed_edges] = (struct OriginalArc){
                .from = u,
                .edge_index = edge_index,
                .lower = lower,
                .cost = cost};

            G->balance[u] -= lower;
            G->balance[v] += lower;
            G->initial_cost += lower * cost;

            if (residual_capacity > G->max_residual_capacity)
                G->max_residual_capacity = residual_capacity;

            ++parsed_edges;
        }
    }

    fclose(fp);

    if (parsed_edges != edge_count)
    {
        fprintf(stderr,
                "Edge count mismatch: header=%" PRId32 " parsed=%" PRId32 "\n",
                edge_count,
                parsed_edges);
        free_graph(G);
        return NULL;
    }

    int64_t balance_sum = 0;
    for (int32_t i = 0; i < node_count; ++i)
        balance_sum += G->balance[i];

    if (balance_sum != 0)
    {
        fprintf(stderr,
                "Adjusted node balances do not sum to zero: %" PRId64 "\n",
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
        for (int32_t i = 0; i < G->node_count; ++i)
            free(G->nodes[i].edges);
    }

    free(G->nodes);
    free(G->balance);
    free(G->original_arcs);
    free(G);
}

/* -------------------------- Delta-active sets -------------------------- */

static void remove_surplus(struct Graph *G, struct ActiveSets *A, int32_t v)
{
    struct Node *node = &G->nodes[v];
    if (!node->in_surplus)
        return;

    int32_t prev = node->s_prev;
    int32_t next = node->s_next;

    if (prev != -1)
        G->nodes[prev].s_next = next;
    else
        A->surplus_head = next;

    if (next != -1)
        G->nodes[next].s_prev = prev;

    node->s_prev = -1;
    node->s_next = -1;
    node->in_surplus = 0;
}

static void remove_deficit(struct Graph *G, struct ActiveSets *A, int32_t v)
{
    struct Node *node = &G->nodes[v];
    if (!node->in_deficit)
        return;

    int32_t prev = node->t_prev;
    int32_t next = node->t_next;

    if (prev != -1)
        G->nodes[prev].t_next = next;
    else
        A->deficit_head = next;

    if (next != -1)
        G->nodes[next].t_prev = prev;

    node->t_prev = -1;
    node->t_next = -1;
    node->in_deficit = 0;
}

static void add_surplus(struct Graph *G, struct ActiveSets *A, int32_t v)
{
    struct Node *node = &G->nodes[v];
    if (node->in_surplus)
        return;

    node->in_surplus = 1;
    node->s_prev = -1;
    node->s_next = A->surplus_head;
    if (A->surplus_head != -1)
        G->nodes[A->surplus_head].s_prev = v;
    A->surplus_head = v;
}

static void add_deficit(struct Graph *G, struct ActiveSets *A, int32_t v)
{
    struct Node *node = &G->nodes[v];
    if (node->in_deficit)
        return;

    node->in_deficit = 1;
    node->t_prev = -1;
    node->t_next = A->deficit_head;
    if (A->deficit_head != -1)
        G->nodes[A->deficit_head].t_prev = v;
    A->deficit_head = v;
}

static void active_sets_rebuild(
    struct Graph *G,
    struct ActiveSets *A,
    int64_t delta)
{
    A->surplus_head = -1;
    A->deficit_head = -1;

    for (int32_t i = 0; i < G->node_count; ++i)
    {
        struct Node *node = &G->nodes[i];
        node->s_next = node->s_prev = -1;
        node->t_next = node->t_prev = -1;
        node->in_surplus = node->in_deficit = 0;

        if (node->excess >= delta)
            add_surplus(G, A, i);
        else if (node->excess <= -delta)
            add_deficit(G, A, i);
    }
}

static void update_membership(
    struct Graph *G,
    struct ActiveSets *A,
    int32_t v,
    int64_t delta)
{
    struct Node *node = &G->nodes[v];

    if (node->in_surplus && node->excess < delta)
        remove_surplus(G, A, v);

    if (node->in_deficit && node->excess > -delta)
        remove_deficit(G, A, v);

    if (!node->in_surplus && !node->in_deficit)
    {
        if (node->excess >= delta)
            add_surplus(G, A, v);
        else if (node->excess <= -delta)
            add_deficit(G, A, v);
    }
}

/* ---------------------------- Binary heap ---------------------------- */

static void heap_init(struct Heap *H, int32_t capacity, int32_t n)
{
    H->node = malloc((size_t)capacity * sizeof(*H->node));
    H->key = malloc((size_t)capacity * sizeof(*H->key));
    H->position = malloc((size_t)n * sizeof(*H->position));

    if (H->node == NULL || H->key == NULL || H->position == NULL)
        allocation_failed();

    H->size = 0;
    H->capacity = capacity;
    for (int32_t i = 0; i < n; ++i)
        H->position[i] = -1;
}

static void heap_free(struct Heap *H)
{
    free(H->node);
    free(H->key);
    free(H->position);
    H->node = NULL;
    H->key = NULL;
    H->position = NULL;
}

static void heap_swap(struct Heap *H, int32_t a, int32_t b)
{
    int32_t vn = H->node[a];
    int64_t vk = H->key[a];

    H->node[a] = H->node[b];
    H->key[a] = H->key[b];
    H->node[b] = vn;
    H->key[b] = vk;

    H->position[H->node[a]] = a;
    H->position[H->node[b]] = b;
}

static void heap_sift_up(struct Heap *H, int32_t pos)
{
    while (pos > 0)
    {
        int32_t parent = (pos - 1) / 2;
        if (H->key[parent] <= H->key[pos])
            break;
        heap_swap(H, parent, pos);
        pos = parent;
    }
}

static void heap_sift_down(struct Heap *H, int32_t pos)
{
    for (;;)
    {
        int32_t left = 2 * pos + 1;
        int32_t right = left + 1;
        int32_t smallest = pos;

        if (left < H->size && H->key[left] < H->key[smallest])
            smallest = left;
        if (right < H->size && H->key[right] < H->key[smallest])
            smallest = right;

        if (smallest == pos)
            break;

        heap_swap(H, pos, smallest);
        pos = smallest;
    }
}

static void heap_insert_or_decrease(
    struct Heap *H,
    int32_t v,
    int64_t key)
{
    int32_t pos = H->position[v];

    if (pos == -1)
    {
        if (H->size >= H->capacity)
            allocation_failed();

        pos = H->size++;
        H->node[pos] = v;
        H->key[pos] = key;
        H->position[v] = pos;
        heap_sift_up(H, pos);
        return;
    }

    if (pos == -2 || key >= H->key[pos])
        return;

    H->key[pos] = key;
    heap_sift_up(H, pos);
}

static int heap_empty(const struct Heap *H)
{
    return H->size == 0;
}

static int32_t heap_pop_min(struct Heap *H, int64_t *out_key)
{
    if (H->size == 0)
        return -1;

    int32_t result = H->node[0];
    *out_key = H->key[0];
    H->position[result] = -2;

    --H->size;
    if (H->size > 0)
    {
        H->node[0] = H->node[H->size];
        H->key[0] = H->key[H->size];
        H->position[H->node[0]] = 0;
        heap_sift_down(H, 0);
    }

    return result;
}

/* -------------------------- Core algorithm -------------------------- */

static int64_t highest_power_of_two(int64_t x)
{
    int64_t p = 1;
    if (x <= 0)
        return 0;

    while (p <= x / 2)
        p *= 2;
    return p;
}

static inline int64_t reduced_cost(
    const struct Graph *G,
    int32_t u,
    const struct Edge *e)
{
    /* c_pi(u,v) = c(u,v) - pi(u) + pi(v). */
    return e->cost - G->nodes[u].potential + G->nodes[e->to_node].potential;
}

static void saturate_negative_reduced_arcs(
    struct Graph *G,
    struct ActiveSets *A,
    int64_t delta)
{
    for (int32_t u = 0; u < G->node_count; ++u)
    {
        struct Node *node = &G->nodes[u];

        for (int32_t i = 0; i < node->edge_count; ++i)
        {
            ++G->negative_arc_edge_scans;

            struct Edge *e = &node->edges[i];
            if (e->residual_capacity < delta)
                continue;

            if (reduced_cost(G, u, e) >= 0)
                continue;

            int32_t v = e->to_node;
            int64_t amount = e->residual_capacity;

            e->residual_capacity = 0;
            G->nodes[v].edges[e->reverse_index].residual_capacity += amount;

            G->nodes[u].excess -= amount;
            G->nodes[v].excess += amount;

            ++G->negative_arc_saturations;

            update_membership(G, A, u, delta);
            if (v != u)
                update_membership(G, A, v, delta);
        }
    }
}

/* Resets only the nodes touched by the previous search. */
static void dijkstra_cleanup(
    int64_t *distance,
    struct Heap *H,
    const int32_t *touched,
    int32_t touched_count)
{
    for (int32_t i = 0; i < touched_count; ++i)
    {
        int32_t v = touched[i];
        distance[v] = INF64;
        H->position[v] = -1;
    }
    H->size = 0;
}

/*
 * Multi-source Dijkstra from every Delta-surplus node (distance 0), stopped
 * as soon as the first Delta-deficit node is settled.
 *
 * Returns 1 when a deficit was found, 0 when none is reachable, and -1 on
 * an internal error.  Nodes whose distance was set are recorded in
 * touched[]; settled nodes are recorded in settled_order[].
 */
static int dijkstra_nearest_deficit(
    struct Graph *G,
    struct ActiveSets *A,
    int64_t delta,
    int32_t *parent_node,
    int32_t *parent_edge,
    int64_t *distance,
    int32_t *settled_order,
    int32_t *settled_count,
    int32_t *touched,
    int32_t *touched_count,
    int32_t *target,
    struct Heap *H)
{
    H->size = 0;
    *settled_count = 0;
    *touched_count = 0;
    *target = -1;

    for (int32_t s = A->surplus_head; s != -1; s = G->nodes[s].s_next)
    {
        distance[s] = 0;
        parent_node[s] = -1;
        parent_edge[s] = -1;
        touched[(*touched_count)++] = s;
        heap_insert_or_decrease(H, s, 0);
        ++G->heap_insert_or_decrease;
    }

    while (!heap_empty(H))
    {
        int64_t du;
        int32_t u = heap_pop_min(H, &du);
        if (u < 0)
            break;

        ++G->heap_pops;
        settled_order[(*settled_count)++] = u;

        if (G->nodes[u].in_deficit)
        {
            *target = u;
            return 1;
        }

        struct Node *node = &G->nodes[u];

        for (int32_t i = 0; i < node->edge_count; ++i)
        {
            ++G->shortest_path_edge_scans;

            struct Edge *e = &node->edges[i];
            if (e->residual_capacity < delta)
                continue;

            int32_t v = e->to_node;
            if (H->position[v] == -2)
                continue;

            int64_t rc = reduced_cost(G, u, e);
            if (rc < 0)
            {
                fprintf(stderr,
                        "Internal error: negative reduced cost=%" PRId64
                        " on Delta edge %" PRId32 " -> %" PRId32 ".\n",
                        rc,
                        u,
                        v);
                return -1;
            }

            int64_t candidate = du + rc;
            if (candidate >= distance[v])
                continue;

            if (distance[v] == INF64)
                touched[(*touched_count)++] = v;

            distance[v] = candidate;
            parent_node[v] = u;
            parent_edge[v] = i;
            heap_insert_or_decrease(H, v, candidate);
            ++G->dijkstra_relaxations;
            ++G->heap_insert_or_decrease;
        }
    }

    return 0;
}

static int64_t compute_total_cost(const struct Graph *G)
{
    int64_t total = G->initial_cost;

    for (int32_t i = 0; i < G->original_edge_count; ++i)
    {
        const struct OriginalArc *arc = &G->original_arcs[i];
        const struct Edge *forward =
            &G->nodes[arc->from].edges[arc->edge_index];
        const struct Edge *reverse =
            &G->nodes[forward->to_node].edges[forward->reverse_index];

        int64_t added_flow = reverse->residual_capacity;
        total += added_flow * arc->cost;
    }

    return total;
}

static int verify_zero_excess(const struct Graph *G)
{
    for (int32_t i = 0; i < G->node_count; ++i)
    {
        if (G->nodes[i].excess != 0)
            return 0;
    }
    return 1;
}

static int64_t run_capacity_scaling(struct Graph *G)
{
    const int32_t n = G->node_count;

    int64_t max_abs_balance = 0;
    for (int32_t i = 0; i < n; ++i)
    {
        int64_t b = G->balance[i];
        int64_t abs_b = b >= 0 ? b : -b;
        if (b == INT64_MIN)
            abs_b = INT64_MAX;
        if (abs_b > max_abs_balance)
            max_abs_balance = abs_b;
    }

    int64_t U = G->max_residual_capacity > max_abs_balance
                    ? G->max_residual_capacity
                    : max_abs_balance;

    if (U <= 0)
    {
        return verify_zero_excess(G) ? G->initial_cost : INT64_MIN;
    }

    int64_t delta = highest_power_of_two(U);
    if (delta <= 0)
        return INT64_MIN;

    int32_t *parent_node = malloc((size_t)n * sizeof(*parent_node));
    int32_t *parent_edge = malloc((size_t)n * sizeof(*parent_edge));
    int64_t *distance = malloc((size_t)n * sizeof(*distance));
    int32_t *settled_order = malloc((size_t)n * sizeof(*settled_order));
    int32_t *touched = malloc((size_t)n * sizeof(*touched));

    if (parent_node == NULL || parent_edge == NULL ||
        distance == NULL || settled_order == NULL || touched == NULL)
    {
        allocation_failed();
    }

    for (int32_t i = 0; i < n; ++i)
    {
        parent_node[i] = -1;
        parent_edge[i] = -1;
        distance[i] = INF64;
    }

    struct Heap H;
    heap_init(&H, n, n);

    for (int32_t i = 0; i < n; ++i)
    {
        G->nodes[i].excess = G->balance[i];
        G->nodes[i].potential = 0;
    }

    int32_t touched_count = 0;
    int ok = 1;

    while (delta >= 1 && ok)
    {
        ++G->scaling_phases;

        struct ActiveSets A;
        active_sets_rebuild(G, &A, delta);

        /* Establish Delta-optimality. */
        saturate_negative_reduced_arcs(G, &A, delta);

        while (A.surplus_head != -1 && A.deficit_head != -1)
        {
            ++G->shortest_path_searches;

            int32_t target = -1;
            int32_t settled_count = 0;

            int found = dijkstra_nearest_deficit(
                G,
                &A,
                delta,
                parent_node,
                parent_edge,
                distance,
                settled_order,
                &settled_count,
                touched,
                &touched_count,
                &target,
                &H);

            if (found < 0)
            {
                ok = 0;
                break;
            }

            if (found == 0)
            {
                ++G->shortest_path_failures;
                dijkstra_cleanup(distance, &H, touched, touched_count);
                touched_count = 0;
                /* No Delta-surplus reaches any Delta-deficit; since nothing
                 * changes until the next phase, the whole phase is done. */
                break;
            }

            ++G->shortest_path_successes;

            int32_t source = target;
            int64_t bottleneck = INF64;

            while (!G->nodes[source].in_surplus)
            {
                int32_t u = parent_node[source];
                int32_t edge_index = parent_edge[source];

                if (u < 0 || edge_index < 0)
                {
                    fprintf(stderr, "Internal error: broken shortest-path predecessor chain.\n");
                    ok = 0;
                    break;
                }

                struct Edge *e = &G->nodes[u].edges[edge_index];
                if (e->residual_capacity < bottleneck)
                    bottleneck = e->residual_capacity;

                source = u;
            }

            if (!ok)
                break;

            if (bottleneck <= 0 || bottleneck == INF64)
            {
                fprintf(stderr, "Internal error: invalid path bottleneck.\n");
                ok = 0;
                break;
            }

            int64_t amount = G->nodes[source].excess;
            if (-G->nodes[target].excess < amount)
                amount = -G->nodes[target].excess;
            if (bottleneck < amount)
                amount = bottleneck;

            if (amount < delta)
            {
                fprintf(stderr,
                        "Internal error: augmentation amount=%" PRId64
                        " smaller than Delta=%" PRId64 ".\n",
                        amount,
                        delta);
                ok = 0;
                break;
            }

            /* Potential update for settled nodes: pi[v] -= d[v] - d[t].
             * Unsettled nodes have distance >= d[t] and keep pi unchanged
             * (equivalent to capping distances at d[t]). */
            int64_t target_distance = distance[target];
            for (int32_t i = 0; i < settled_count; ++i)
            {
                int32_t v = settled_order[i];
                G->nodes[v].potential -= distance[v] - target_distance;
            }

            /* Augment the complete feasible amount on the path. */
            int32_t v = target;
            while (v != source)
            {
                int32_t u = parent_node[v];
                int32_t edge_index = parent_edge[v];
                struct Edge *e = &G->nodes[u].edges[edge_index];

                e->residual_capacity -= amount;
                G->nodes[v].edges[e->reverse_index].residual_capacity += amount;
                ++G->path_edge_augmentations;

                v = u;
            }

            G->nodes[source].excess -= amount;
            G->nodes[target].excess += amount;
            ++G->augmentations;

            update_membership(G, &A, source, delta);
            update_membership(G, &A, target, delta);

            dijkstra_cleanup(distance, &H, touched, touched_count);
            touched_count = 0;
        }

        delta /= 2;
    }

    heap_free(&H);
    free(parent_node);
    free(parent_edge);
    free(distance);
    free(settled_order);
    free(touched);

    if (!ok)
        return INT64_MIN;

    if (!verify_zero_excess(G))
    {
        fprintf(stderr,
                "No feasible flow exists: residual node balances remain nonzero.\n");
        return INT64_MIN;
    }

    int64_t total_cost = compute_total_cost(G);
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
    int64_t total_cost = run_capacity_scaling(G);
    clock_gettime(CLOCK_MONOTONIC, &end);

    if (total_cost == INT64_MIN)
    {
        free_graph(G);
        return 2;
    }

    int64_t execution_time_us = elapsed_microseconds(&start, &end);

    printf("Minimum cost = %" PRId64 "\n", total_cost);
    printf("Execution time = %" PRId64 " us\n", execution_time_us);
    printf("Scaling phases = %" PRId64 "\n", G->scaling_phases);
    printf("Negative arc saturations = %" PRId64 "\n", G->negative_arc_saturations);
    printf("Negative arc edge scans = %" PRId64 "\n", G->negative_arc_edge_scans);
    printf("Shortest-path searches = %" PRId64 "\n", G->shortest_path_searches);
    printf("Shortest-path successes = %" PRId64 "\n", G->shortest_path_successes);
    printf("Shortest-path failures = %" PRId64 "\n", G->shortest_path_failures);
    printf("Shortest-path edge scans = %" PRId64 "\n", G->shortest_path_edge_scans);
    printf("Dijkstra relaxations = %" PRId64 "\n", G->dijkstra_relaxations);
    printf("Heap insert/decrease operations = %" PRId64 "\n", G->heap_insert_or_decrease);
    printf("Heap pops = %" PRId64 "\n", G->heap_pops);
    printf("Augmentations = %" PRId64 "\n", G->augmentations);
    printf("Path edge augmentations = %" PRId64 "\n", G->path_edge_augmentations);

    printf("\nBENCH_RESULT algorithm=Capacity-Scaling "
           "nodes=%" PRId32 " edges=%" PRId32 " "
           "total_cost=%" PRId64 " execution_time_us=%" PRId64 " "
           "scaling_phases=%" PRId64 " "
           "negative_arc_saturations=%" PRId64 " "
           "negative_arc_edge_scans=%" PRId64 " "
           "shortest_path_searches=%" PRId64 " "
           "shortest_path_successes=%" PRId64 " "
           "shortest_path_failures=%" PRId64 " "
           "shortest_path_edge_scans=%" PRId64 " "
           "dijkstra_relaxations=%" PRId64 " "
           "heap_insert_or_decrease=%" PRId64 " "
           "heap_pops=%" PRId64 " "
           "augmentations=%" PRId64 " "
           "path_edge_augmentations=%" PRId64 "\n",
           G->node_count,
           G->original_edge_count,
           total_cost,
           execution_time_us,
           G->scaling_phases,
           G->negative_arc_saturations,
           G->negative_arc_edge_scans,
           G->shortest_path_searches,
           G->shortest_path_successes,
           G->shortest_path_failures,
           G->shortest_path_edge_scans,
           G->dijkstra_relaxations,
           G->heap_insert_or_decrease,
           G->heap_pops,
           G->augmentations,
           G->path_edge_augmentations);

    free_graph(G);
    return 0;
}