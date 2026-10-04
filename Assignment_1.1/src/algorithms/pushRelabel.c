#define _POSIX_C_SOURCE 200809L

#include <stdio.h>

#include <stdlib.h>

#include <stdint.h>

#include <string.h>

#include <time.h>

#include <limits.h>

#define MAX_LINE_SIZE 1024

#define GR_FREQ_DIVISOR 2

struct Edge

{

    int32_t to_node;

    int32_t reverse_index;

    int64_t residual_capacity;
};

struct Node

{

    int32_t height;

    int64_t excess;

    struct Edge *edges;

    int32_t edge_count;

    int32_t edge_capacity;

    /* Highest-label active-vertex bucket links. */

    int32_t next_active;

    int32_t prev_active;

    int8_t active;
};

struct Graph

{

    int32_t node_count;

    int32_t original_edge_count;

    struct Node *nodes;

    int32_t source;

    int32_t sink;

    /* Algorithm-specific counters. */

    int64_t push_operations;

    int64_t relabel_operations;

    int64_t discharge_operations;

    int64_t edge_scans;

    int64_t work_since_global_relabel;

    int64_t global_relabels;

    int64_t gap_operations;
};

struct ActiveBuckets

{

    int32_t *head;

    int32_t max_height;

    int32_t highest_active_height;

    int32_t active_count;
};

static void free_graph(struct Graph *G);

static struct Graph *build_graph_from_dataset(const char *filename)

{

    struct Graph *G = calloc(1, sizeof(*G));

    if (!G)

        return NULL;

    G->source = -1;

    G->sink = -1;

    FILE *fp = fopen(filename, "r");

    if (!fp)

    {

        free(G);

        return NULL;
    }

    char line[MAX_LINE_SIZE];

    while (fgets(line, sizeof(line), fp))

    {

        char *token = strtok(line, " \t\n");

        if (!token)

            continue;

        /*

         * p <problem_type> <node_count> <edge_count>

         */

        if (strcmp(token, "p") == 0)

        {

            token = strtok(NULL, " \t\n");

            token = strtok(NULL, " \t\n");

            char *edge_token =

                strtok(NULL, " \t\n");

            if (!token || !edge_token)

            {

                fclose(fp);

                free_graph(G);

                return NULL;
            }

            G->node_count =

                atoi(token);

            G->original_edge_count =

                atoi(edge_token);

            G->nodes =

                calloc(

                    G->node_count,

                    sizeof(struct Node));

            if (!G->nodes)

            {

                fclose(fp);

                free_graph(G);

                return NULL;
            }
        }

        /*

         * a <from> <to> <capacity>

         */

        else if (strcmp(token, "a") == 0)

        {

            char *u_token =

                strtok(NULL, " \t\n");

            char *v_token =

                strtok(NULL, " \t\n");

            char *c_token =

                strtok(NULL, " \t\n");

            if (!u_token ||

                !v_token ||

                !c_token)

            {

                continue;
            }

            int32_t u =

                atoi(u_token) - 1;

            int32_t v =

                atoi(v_token) - 1;

            int64_t capacity =

                atoll(c_token);

            if (capacity > 0 &&

                u >= 0 &&

                u < G->node_count &&

                v >= 0 &&

                v < G->node_count)

            {

                struct Node *U =

                    &G->nodes[u];

                struct Node *V =

                    &G->nodes[v];

                /*

                 * Expand U's edge array.

                 */

                if (U->edge_count ==

                    U->edge_capacity)

                {

                    int32_t new_capacity =

                        U->edge_capacity

                            ? U->edge_capacity * 2

                            : 4;

                    struct Edge *tmp =

                        realloc(

                            U->edges,

                            (size_t)new_capacity *

                                sizeof(struct Edge));

                    if (!tmp)

                    {

                        fclose(fp);

                        free_graph(G);

                        return NULL;
                    }

                    U->edges = tmp;

                    U->edge_capacity =

                        new_capacity;
                }

                /*

                 * Expand V's edge array.

                 */

                if (V->edge_count ==

                    V->edge_capacity)

                {

                    int32_t new_capacity =

                        V->edge_capacity

                            ? V->edge_capacity * 2

                            : 4;

                    struct Edge *tmp =

                        realloc(

                            V->edges,

                            (size_t)new_capacity *

                                sizeof(struct Edge));

                    if (!tmp)

                    {

                        fclose(fp);

                        free_graph(G);

                        return NULL;
                    }

                    V->edges = tmp;

                    V->edge_capacity =

                        new_capacity;
                }

                int32_t forward_index =

                    U->edge_count;

                int32_t reverse_index =

                    V->edge_count;

                /*

                 * Forward residual edge.

                 */

                U->edges[forward_index].to_node =

                    v;

                U->edges[forward_index].reverse_index =

                    reverse_index;

                U->edges[forward_index].residual_capacity =

                    capacity;

                /*

                 * Reverse residual edge.

                 */

                V->edges[reverse_index].to_node =

                    u;

                V->edges[reverse_index].reverse_index =

                    forward_index;

                V->edges[reverse_index].residual_capacity =

                    0;

                U->edge_count++;

                V->edge_count++;
            }
        }

        /*

         * n <node> s

         * n <node> t

         */

        else if (strcmp(token, "n") == 0)

        {

            char *id_token =

                strtok(NULL, " \t\n");

            char *type_token =

                strtok(NULL, " \t\n");

            if (!id_token ||

                !type_token)

            {

                continue;
            }

            int32_t node =

                atoi(id_token) - 1;

            if (strcmp(type_token, "s") == 0)

            {

                G->source = node;
            }

            else if (strcmp(type_token, "t") == 0)

            {

                G->sink = node;
            }
        }
    }

    fclose(fp);

    if (!G->nodes ||

        G->node_count <= 0 ||

        G->source < 0 ||

        G->sink < 0)

    {

        free_graph(G);

        return NULL;
    }

    for (int32_t i = 0;

         i < G->node_count;

         ++i)

    {

        G->nodes[i].next_active = -1;

        G->nodes[i].prev_active = -1;
    }

    return G;
}

static void free_graph(struct Graph *G)

{

    if (!G)

        return;

    if (G->nodes)

    {

        for (int32_t i = 0;

             i < G->node_count;

             ++i)

        {

            free(G->nodes[i].edges);
        }

        free(G->nodes);
    }

    free(G);
}

/* ============================================================

 * Highest-label active vertex buckets

 * ============================================================ */

static void bucket_init(

    struct ActiveBuckets *B,

    int32_t max_height)

{

    B->head =

        malloc(

            (size_t)(max_height + 1) *

            sizeof(int32_t));

    if (!B->head)

    {

        fprintf(

            stderr,

            "Memory allocation failed.\n");

        exit(EXIT_FAILURE);
    }

    for (int32_t i = 0;

         i <= max_height;

         ++i)

    {

        B->head[i] = -1;
    }

    B->max_height =

        max_height;

    B->highest_active_height =

        -1;

    B->active_count =

        0;
}

static void bucket_free(

    struct ActiveBuckets *B)

{

    free(B->head);

    B->head =

        NULL;
}

static void enqueue_active(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t v)

{

    struct Node *node =

        &G->nodes[v];

    if (node->active ||

        node->excess <= 0 ||

        v == G->source ||

        v == G->sink)

    {

        return;
    }

    int32_t h =

        node->height;

    if (h < 0 ||

        h > B->max_height)

    {

        return;
    }

    node->active =

        1;

    node->prev_active =

        -1;

    node->next_active =

        B->head[h];

    if (B->head[h] != -1)

    {

        G->nodes[B->head[h]].prev_active = v;
    }

    B->head[h] =

        v;

    B->active_count++;

    if (h >

        B->highest_active_height)

    {

        B->highest_active_height =

            h;
    }
}

static void remove_active(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t v)

{

    struct Node *node =

        &G->nodes[v];

    if (!node->active)

        return;

    int32_t h =

        node->height;

    int32_t prev =

        node->prev_active;

    int32_t next =

        node->next_active;

    if (prev != -1)

    {

        G->nodes[prev].next_active =

            next;
    }

    else

    {

        B->head[h] =

            next;
    }

    if (next != -1)

    {

        G->nodes[next].prev_active =

            prev;
    }

    node->prev_active =

        -1;

    node->next_active =

        -1;

    node->active =

        0;

    B->active_count--;
}

static int32_t pop_highest_active(

    struct Graph *G,

    struct ActiveBuckets *B)

{

    while (

        B->highest_active_height >= 0 &&

        B->highest_active_height <= B->max_height &&

        B->head[B->highest_active_height] == -1)

    {

        B->highest_active_height--;
    }

    if (B->highest_active_height < 0)

        return -1;

    int32_t v =

        B->head[B->highest_active_height];

    remove_active(

        G,

        B,

        v);

    return v;
}

/* ============================================================

 * Push

 * ============================================================ */

static void push_flow(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t u,

    int32_t edge_index,

    int64_t amount)

{

    struct Edge *edge =

        &G->nodes[u].edges[edge_index];

    int32_t v =

        edge->to_node;

    int32_t reverse_index =

        edge->reverse_index;

    /*

     * Destination becomes active exactly when

     * its excess changes from zero to positive.

     */

    int8_t was_zero =

        (G->nodes[v].excess == 0);

    /*

     * Forward residual capacity decreases.

     */

    edge->residual_capacity -=

        amount;

    /*

     * Reverse residual capacity increases.

     */

    G->nodes[v]

        .edges[reverse_index]

        .residual_capacity +=

        amount;

    /*

     * Excess update.

     */

    G->nodes[u].excess -=

        amount;

    G->nodes[v].excess +=

        amount;

    G->push_operations++;

    /*

     * Activate destination immediately.

     */

    if (was_zero &&

        G->nodes[v].excess > 0)

    {

        enqueue_active(

            G,

            B,

            v);
    }
}

/* ============================================================

 * Global relabel

 * ============================================================ */

static void global_relabel(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t *current_edge,

    int32_t *height_count)

{

    const int32_t n =

        G->node_count;

    int32_t *queue =

        malloc(

            (size_t)n *

            sizeof(int32_t));

    if (!queue)

    {

        fprintf(

            stderr,

            "Memory allocation failed.\n");

        exit(EXIT_FAILURE);
    }

    /*

     * Initially every vertex is considered

     * unreachable from the sink.

     */

    for (int32_t i = 0;

         i < n;

         ++i)

    {

        G->nodes[i].height =

            n + 1;

        current_edge[i] =

            0;
    }

    int32_t front =

        0;

    int32_t rear =

        0;

    /*

     * Sink has distance zero.

     */

    G->nodes[G->sink].height =

        0;

    queue[rear++] =

        G->sink;

    /*

     * BFS through reverse residual edges.

     *

     * If u -> v has residual capacity,

     * then from the sink's perspective we can

     * traverse v -> u.

     */

    while (front < rear)

    {

        int32_t v =

            queue[front++];

        struct Node *node =

            &G->nodes[v];

        for (int32_t i = 0;

             i < node->edge_count;

             ++i)

        {

            G->edge_scans++;

            struct Edge *reverse_view =

                &node->edges[i];

            int32_t u =

                reverse_view->to_node;

            /*

             * reverse_view is the edge v -> u.

             * Its reverse edge is u -> v.

             */

            struct Edge *u_to_v =

                &G->nodes[u]

                     .edges[reverse_view->reverse_index];

            if (u_to_v->residual_capacity <= 0)

                continue;

            /*

             * Source must keep height n.

             */

            if (u == G->source)

                continue;

            if (G->nodes[u].height == n + 1)

            {

                G->nodes[u].height =

                    node->height + 1;

                queue[rear++] =

                    u;
            }
        }
    }

    /*

     * Source height is always n.

     */

    G->nodes[G->source].height =

        n;

    /*

     * Rebuild active buckets because heights

     * have changed globally.

     */

    for (int32_t h = 0;

         h <= B->max_height;

         ++h)

    {

        B->head[h] =

            -1;
    }

    B->highest_active_height =

        -1;

    B->active_count =

        0;

    for (int32_t i = 0;

         i < n;

         ++i)

    {

        G->nodes[i].active =

            0;

        G->nodes[i].next_active =

            -1;

        G->nodes[i].prev_active =

            -1;
    }

    for (int32_t i = 0;

         i < n;

         ++i)

    {

        if (i == G->source ||

            i == G->sink)

        {

            continue;
        }

        if (G->nodes[i].excess > 0)

        {

            enqueue_active(

                G,

                B,

                i);
        }
    }

    memset(height_count, 0, (size_t)(2 * n + 2) * sizeof(int32_t));
    for (int32_t i = 0; i < n; ++i)
    {
        height_count[G->nodes[i].height]++;
    }
    G->global_relabels++;

    /*

     * This work should not count toward the

     * threshold for the next global relabel.

     */

    G->work_since_global_relabel =

        0;

    free(queue);
}

/* ============================================================

 * Gap heuristic

 * ============================================================ */

static void gap_relabel(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t height,

    int32_t *height_count,

    int32_t skip)

{

    const int32_t n =

        G->node_count;

    /*

     * If there is no vertex at height h,

     * every vertex above h but below n cannot

     * reach the sink through admissible paths.

     *

     * Move those vertices to n + 1.

     */

    for (int32_t v = 0;

         v < n;

         ++v)

    {

        if (v == G->source ||

            v == G->sink)

        {

            continue;
        }

        if (G->nodes[v].height > height &&

            G->nodes[v].height < n)

        {

            int32_t old_height =

                G->nodes[v].height;

            if (G->nodes[v].active)

            {

                remove_active(

                    G,

                    B,

                    v);
            }

            height_count[old_height]--;

            G->nodes[v].height =

                n + 1;

            height_count[n + 1]++;

            if (v != skip && G->nodes[v].excess > 0)
            {
                enqueue_active(G, B, v);
            }
        }
    }

    G->gap_operations++;
}

/* ============================================================

 * Relabel

 * ============================================================ */

static void relabel_node(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t u,

    int32_t *height_count)

{

    struct Node *node =

        &G->nodes[u];

    int32_t min_height =

        INT32_MAX;

    /*

     * Find the minimum neighbour height over

     * all residual outgoing edges.

     */

    for (int32_t i = 0;

         i < node->edge_count;

         ++i)

    {

        G->edge_scans++;

        G->work_since_global_relabel++;

        struct Edge *edge =

            &node->edges[i];

        if (edge->residual_capacity > 0)

        {

            int32_t h =

                G->nodes[edge->to_node].height;

            if (h < min_height)

            {

                min_height =

                    h;
            }
        }
    }

    int32_t old_height =

        node->height;

    if (min_height != INT32_MAX)

    {

        int32_t new_height =

            min_height + 1;

        if (old_height >= 0 &&

            old_height <=

                G->node_count + 1)

        {

            height_count[old_height]--;
        }

        node->height =

            new_height;

        height_count[new_height]++;
    }

    G->relabel_operations++;

    /*

     * Gap heuristic.

     */

    if (old_height > 0 &&

        old_height < G->node_count &&

        height_count[old_height] == 0)

    {

        gap_relabel(
            G,
            B,
            old_height,
            height_count,
            u);
    }
}

/* ============================================================

 * Discharge

 * ============================================================ */

static void discharge(

    struct Graph *G,

    struct ActiveBuckets *B,

    int32_t u,

    int32_t *current_edge,

    int32_t *height_count)

{

    struct Node *node =

        &G->nodes[u];

    G->discharge_operations++;

    /*

     * Continue until u has no excess.

     */

    while (node->excess > 0)

    {

        /*

         * All edges have been exhausted.

         * Relabel u and restart its adjacency scan.

         */

        if (current_edge[u] >=

            node->edge_count)

        {

            int32_t old_height =

                node->height;

            relabel_node(

                G,

                B,

                u,

                height_count);

            current_edge[u] =

                0;

            /*

             * Defensive condition:

             * if no valid relabel was possible,

             * stop rather than loop forever.

             */

            if (node->height ==

                old_height)

            {

                break;
            }

            continue;
        }

        int32_t edge_index =

            current_edge[u];

        struct Edge *edge =

            &node->edges[edge_index];

        G->edge_scans++;

        G->work_since_global_relabel++;

        int32_t v =

            edge->to_node;

        /*

         * Admissible edge:

         *

         * residual capacity > 0

         * height[u] = height[v] + 1

         */

        if (edge->residual_capacity > 0 &&

            node->height ==

                G->nodes[v].height + 1)

        {

            int64_t amount =

                node->excess;

            if (amount >

                edge->residual_capacity)

            {

                amount =

                    edge->residual_capacity;
            }

            push_flow(

                G,

                B,

                u,

                edge_index,

                amount);
        }

        else

        {

            /*

             * Current arc is exhausted for now.

             */

            current_edge[u]++;
        }
    }
}

/* ============================================================

 * Push-Relabel

 * ============================================================ */

static int64_t run_push_relabel(

    struct Graph *G)

{

    const int32_t n =

        G->node_count;

    if (G->source ==

        G->sink)

    {

        return 0;
    }

    /*

     * Current-arc pointer.

     */

    int32_t *current_edge =

        calloc(

            (size_t)n,

            sizeof(int32_t));

    /*

     * Height counts for gap heuristic.

     *

     * Heights can range up to 2n.

     */

    int32_t *height_count =

        calloc(

            (size_t)(2 * n + 2),

            sizeof(int32_t));

    if (!current_edge ||

        !height_count)

    {

        fprintf(

            stderr,

            "Memory allocation failed.\n");

        free(current_edge);

        free(height_count);

        exit(EXIT_FAILURE);
    }

    struct ActiveBuckets buckets;

    /*

     * Heights can theoretically reach 2n.

     */

    bucket_init(

        &buckets,

        2 * n);

    /*

     * --------------------------------------------------------

     * INITIAL PRE-FLOW

     * --------------------------------------------------------

     */

    for (int32_t i = 0;

         i < n;

         ++i)

    {

        G->nodes[i].height =

            0;
    }

    /*

     * Source has height n.

     */

    G->nodes[G->source].height =

        n;

    /*

     * Initial height counts.

     */

    height_count[0] =

        n - 1;

    height_count[n] =

        1;

    /*

     * Saturate every edge leaving source.

     */

    for (int32_t i = 0;

         i < G->nodes[G->source].edge_count;

         ++i)

    {

        struct Edge *edge =

            &G->nodes[G->source]

                 .edges[i];

        if (edge->residual_capacity <= 0)

            continue;

        int32_t v =

            edge->to_node;

        int64_t amount =

            edge->residual_capacity;

        int32_t reverse_index =

            edge->reverse_index;

        /*

         * Saturate forward edge.

         */

        edge->residual_capacity =

            0;

        /*

         * Create reverse residual capacity.

         */

        G->nodes[v]

            .edges[reverse_index]

            .residual_capacity +=

            amount;

        /*

         * Update excess.

         */

        G->nodes[G->source].excess -=

            amount;

        int8_t was_zero =

            (G->nodes[v].excess == 0);

        G->nodes[v].excess +=

            amount;

        /*

         * Initial source pushes are also push

         * operations for benchmarking.

         */

        G->push_operations++;

        if (was_zero &&

            v != G->source &&

            v != G->sink)

        {

            enqueue_active(

                G,

                &buckets,

                v);
        }
    }

    /*

     * --------------------------------------------------------

     * INITIAL GLOBAL RELABEL

     * --------------------------------------------------------

     *

     * Give vertices accurate distances from the sink

     * before beginning the main discharge process.

     */

    global_relabel(G, &buckets, current_edge, height_count);

    /*

     * Perform a global relabel after roughly 2m units of

     * local edge-processing work.

     */

    int64_t relabel_threshold =

        (int64_t)(G->original_edge_count > 0

                      ? G->original_edge_count

                      : 1) *

        GR_FREQ_DIVISOR;

    /*

     * --------------------------------------------------------

     * MAIN LOOP

     * --------------------------------------------------------

     */

    while (buckets.active_count > 0)

    {

        /*

         * Highest-label active vertex.

         */

        int32_t u =

            pop_highest_active(

                G,

                &buckets);

        if (u < 0)

            break;

        /*

         * Discharge it.

         */

        discharge(

            G,

            &buckets,

            u,

            current_edge,

            height_count);

        /*

         * If it still has excess,

         * reactivate it.

         */

        if (G->nodes[u].excess > 0)

        {

            enqueue_active(

                G,

                &buckets,

                u);
        }

        /*

         * Periodic global relabeling.

         */

        if (G->work_since_global_relabel >=

            relabel_threshold)

        {

            global_relabel(G, &buckets, current_edge, height_count);
        }
    }

    /*

     * When there are no active vertices,

     * the preflow is a valid flow.

     *

     * The sink's excess is the maximum flow.

     */

    int64_t max_flow =

        G->nodes[G->sink].excess;

    bucket_free(

        &buckets);

    free(current_edge);

    free(height_count);

    return max_flow;
}

/* ============================================================

 * Main

 * ============================================================ */

int main(

    int argc,

    char *argv[])

{

    if (argc != 2)

    {

        fprintf(

            stderr,

            "Usage: %s <dataset-file>\n",

            argv[0]);

        return 1;
    }

    struct Graph *G =

        build_graph_from_dataset(

            argv[1]);

    if (!G)

    {

        fprintf(

            stderr,

            "Failed to build graph.\n");

        return 1;
    }

    printf(

        "Number of nodes = %d\n",

        G->node_count);

    printf(

        "Source node = %d\n",

        G->source + 1);

    printf(

        "Sink node = %d\n",

        G->sink + 1);

    /*

     * Only measure the algorithm.

     *

     * Dataset parsing and graph construction

     * are outside this timing region.

     */

    struct timespec start;

    struct timespec end;

    clock_gettime(

        CLOCK_MONOTONIC,

        &start);

    int64_t max_flow =

        run_push_relabel(

            G);

    clock_gettime(

        CLOCK_MONOTONIC,

        &end);

    int64_t execution_time_us =

        (end.tv_sec - start.tv_sec) *

            1000000LL +

        (end.tv_nsec - start.tv_nsec) /

            1000LL;

    printf(

        "\nMax flow = %lld\n",

        (long long)max_flow);

    printf(

        "Execution time = %lld us\n",

        (long long)execution_time_us);

    printf(

        "Push operations = %lld\n",

        (long long)G->push_operations);

    printf(

        "Relabel operations = %lld\n",

        (long long)G->relabel_operations);

    printf(

        "Discharge operations = %lld\n",

        (long long)G->discharge_operations);

    printf(

        "Edge scans = %lld\n",

        (long long)G->edge_scans);

    printf(

        "Global relabels = %lld\n",

        (long long)G->global_relabels);

    printf(

        "Gap operations = %lld\n",

        (long long)G->gap_operations);

    /*
     * Machine-readable single-line output for benchmark.c.
     *
     * All key=value fields must remain on this line because
     * benchmark.c parses the complete record from one line.
     */
    printf(
        "BENCH_RESULT "
        "algorithm=Push-Relabel "
        "nodes=%lld "
        "edges=%lld "
        "max_flow=%lld "
        "execution_time_us=%lld "
        "bfs_phases=0 "
        "dfs_calls=0 "
        "augmentations=0 "
        "push_operations=%lld "
        "relabel_operations=%lld "
        "discharge_operations=%lld "
        "edge_scans=%lld "
        "global_relabels=%lld "
        "gap_operations=%lld\n",
        (long long)G->node_count,
        (long long)G->original_edge_count,
        (long long)max_flow,
        (long long)execution_time_us,
        (long long)G->push_operations,
        (long long)G->relabel_operations,
        (long long)G->discharge_operations,
        (long long)G->edge_scans,
        (long long)G->global_relabels,
        (long long)G->gap_operations);
    free_graph(G);

    return 0;
}