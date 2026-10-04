#define _POSIX_C_SOURCE 200809L

#include <stdio.h>

#include <stdlib.h>

#include <stdint.h>

#include <string.h>

#include <time.h>

#include <limits.h>

#define MAX_LINE_SIZE 1024

struct Edge

{

    int32_t to_node;

    int32_t reverse_index;

    int64_t residual_capacity;
};

struct Node

{

    int32_t id;

    int32_t level;

    struct Edge *edges;

    int32_t edge_count;

    int32_t edge_capacity;
};

struct Graph

{

    int32_t node_count;

    int32_t original_edge_count;

    struct Node *nodes;

    int32_t source;

    int32_t sink;

    /* Algorithm-specific statistics */

    int64_t bfs_phases;

    int64_t dfs_calls;

    int64_t augmentations;

    int64_t edge_scans;
};

/* ---------- Function declarations ---------- */

static struct Graph *build_graph_from_dataset(

    const char *);

static void add_edge(

    struct Graph *,

    int32_t,

    int32_t,

    int64_t);

static int32_t bfs(

    struct Graph *);

static int64_t dfs(

    struct Graph *,

    int32_t,

    int64_t,

    int32_t *);

static int64_t run_dinic(

    struct Graph *);

static void free_graph(

    struct Graph *);

/* ============================================================

 * Main

 * ============================================================ */

int main(int argc, char *argv[])

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

        build_graph_from_dataset(argv[1]);

    if (G == NULL)

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

     * Measure only the actual Dinic algorithm.

     *

     * Dataset parsing and graph construction

     * are deliberately outside the timing region.

     */

    struct timespec start;

    struct timespec end;

    clock_gettime(

        CLOCK_MONOTONIC,

        &start);

    int64_t max_flow =

        run_dinic(G);

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

        "BFS phases = %lld\n",

        (long long)G->bfs_phases);

    printf(

        "DFS calls = %lld\n",

        (long long)G->dfs_calls);

    printf(

        "Augmentations = %lld\n",

        (long long)G->augmentations);

    printf(

        "Edge scans = %lld\n",

        (long long)G->edge_scans);

    /*
     * Machine-readable single-line output for benchmark.c.
     *
     * All key=value fields must remain on this line because
     * benchmark.c parses the complete record from one line.
     */
    printf(
        "BENCH_RESULT "
        "algorithm=Dinic "
        "nodes=%lld "
        "edges=%lld "
        "max_flow=%lld "
        "execution_time_us=%lld "
        "bfs_phases=%lld "
        "dfs_calls=%lld "
        "augmentations=%lld "
        "push_operations=0 "
        "relabel_operations=0 "
        "discharge_operations=0 "
        "edge_scans=%lld\n",
        (long long)G->node_count,
        (long long)G->original_edge_count,
        (long long)max_flow,
        (long long)execution_time_us,
        (long long)G->bfs_phases,
        (long long)G->dfs_calls,
        (long long)G->augmentations,
        (long long)G->edge_scans);
    free_graph(G);

    return 0;
}

/* ============================================================

 * Graph construction

 * ============================================================ */

static struct Graph *build_graph_from_dataset(

    const char *filename)

{

    struct Graph *G =

        calloc(1, sizeof(struct Graph));

    if (G == NULL)

        return NULL;

    G->node_count = -1;

    G->original_edge_count = 0;

    G->nodes = NULL;

    G->source = -1;

    G->sink = -1;

    G->bfs_phases = 0;

    G->dfs_calls = 0;

    G->augmentations = 0;

    G->edge_scans = 0;

    FILE *fp =

        fopen(filename, "r");

    if (fp == NULL)

    {

        free(G);

        return NULL;
    }

    char line[MAX_LINE_SIZE];

    while (fgets(

               line,

               sizeof(line),

               fp) != NULL)

    {

        char *token =

            strtok(line, " \t\n");

        if (token == NULL)

            continue;

        /*

         * p <problem_type> <nodes> <edges>

         */

        if (strcmp(token, "p") == 0)

        {

            char *problem_type =

                strtok(NULL, " \t\n");

            char *node_token =

                strtok(NULL, " \t\n");

            char *edge_token =

                strtok(NULL, " \t\n");

            (void)problem_type;

            if (node_token == NULL ||

                edge_token == NULL)

            {

                fclose(fp);

                free_graph(G);

                return NULL;
            }

            G->node_count =

                atoi(node_token);

            G->original_edge_count =

                atoi(edge_token);

            G->nodes =

                calloc(

                    (size_t)G->node_count,

                    sizeof(struct Node));

            if (G->nodes == NULL)

            {

                fclose(fp);

                free(G);

                return NULL;
            }

            for (int32_t i = 0;

                 i < G->node_count;

                 ++i)

            {

                G->nodes[i].id =

                    i;

                G->nodes[i].level =

                    -1;

                G->nodes[i].edges =

                    NULL;

                G->nodes[i].edge_count =

                    0;

                G->nodes[i].edge_capacity =

                    0;
            }
        }

        /*

         * a <from> <to> <capacity>

         */

        else if (strcmp(token, "a") == 0)

        {

            char *from_token =

                strtok(NULL, " \t\n");

            char *to_token =

                strtok(NULL, " \t\n");

            char *capacity_token =

                strtok(NULL, " \t\n");

            if (from_token == NULL ||

                to_token == NULL ||

                capacity_token == NULL)

            {

                continue;
            }

            int32_t from =

                atoi(from_token) - 1;

            int32_t to =

                atoi(to_token) - 1;

            int64_t capacity =

                atoll(capacity_token);

            if (capacity <= 0)

                continue;

            if (from < 0 ||

                from >= G->node_count ||

                to < 0 ||

                to >= G->node_count)

            {

                continue;
            }

            add_edge(

                G,

                from,

                to,

                capacity);
        }

        /*

         * n <node> s

         * n <node> t

         */

        else if (strcmp(token, "n") == 0)

        {

            char *node_token =

                strtok(NULL, " \t\n");

            char *type_token =

                strtok(NULL, " \t\n");

            if (node_token == NULL ||

                type_token == NULL)

            {

                continue;
            }

            int32_t node =

                atoi(node_token) - 1;

            if (node < 0 ||

                node >= G->node_count)

            {

                continue;
            }

            if (strcmp(type_token, "s") == 0)

            {

                G->source =

                    node;
            }

            else if (strcmp(type_token, "t") == 0)

            {

                G->sink =

                    node;
            }
        }
    }

    fclose(fp);

    if (G->nodes == NULL ||

        G->node_count <= 0 ||

        G->source < 0 ||

        G->sink < 0)

    {

        free_graph(G);

        return NULL;
    }

    return G;
}

/* ============================================================

 * Add forward/reverse residual edge pair

 * ============================================================ */

static void add_edge(

    struct Graph *G,

    int32_t from,

    int32_t to,

    int64_t capacity)

{

    struct Node *from_node =

        &G->nodes[from];

    struct Node *to_node =

        &G->nodes[to];

    /*

     * Expand source adjacency list.

     */

    if (from_node->edge_count ==

        from_node->edge_capacity)

    {

        int32_t new_capacity =

            from_node->edge_capacity == 0

                ? 4

                : from_node->edge_capacity * 2;

        struct Edge *new_edges =

            realloc(

                from_node->edges,

                (size_t)new_capacity *

                    sizeof(struct Edge));

        if (new_edges == NULL)

        {

            fprintf(

                stderr,

                "Memory allocation failed.\n");

            exit(EXIT_FAILURE);
        }

        from_node->edges =

            new_edges;

        from_node->edge_capacity =

            new_capacity;
    }

    /*

     * Expand destination adjacency list.

     */

    if (to_node->edge_count ==

        to_node->edge_capacity)

    {

        int32_t new_capacity =

            to_node->edge_capacity == 0

                ? 4

                : to_node->edge_capacity * 2;

        struct Edge *new_edges =

            realloc(

                to_node->edges,

                (size_t)new_capacity *

                    sizeof(struct Edge));

        if (new_edges == NULL)

        {

            fprintf(

                stderr,

                "Memory allocation failed.\n");

            exit(EXIT_FAILURE);
        }

        to_node->edges =

            new_edges;

        to_node->edge_capacity =

            new_capacity;
    }

    int32_t forward_index =

        from_node->edge_count;

    int32_t reverse_index =

        to_node->edge_count;

    /*

     * Forward edge.

     */

    from_node->edges[forward_index].to_node =

        to;

    from_node->edges[forward_index].reverse_index =

        reverse_index;

    from_node->edges[forward_index].residual_capacity =

        capacity;

    /*

     * Reverse edge.

     */

    to_node->edges[reverse_index].to_node =

        from;

    to_node->edges[reverse_index].reverse_index =

        forward_index;

    to_node->edges[reverse_index].residual_capacity =

        0;

    from_node->edge_count++;

    to_node->edge_count++;
}

/* ============================================================

 * BFS

 * ============================================================ */

static int32_t bfs(

    struct Graph *G)

{

    const int32_t n =

        G->node_count;

    const int32_t source =

        G->source;

    const int32_t sink =

        G->sink;

    /*

     * Reset levels.

     */

    for (int32_t i = 0;

         i < n;

         ++i)

    {

        G->nodes[i].level =

            -1;
    }

    int32_t *queue =

        malloc(

            (size_t)n *

            sizeof(int32_t));

    if (queue == NULL)

    {

        fprintf(

            stderr,

            "Memory allocation failed.\n");

        exit(EXIT_FAILURE);
    }

    int32_t front = 0;

    int32_t rear = 0;

    G->nodes[source].level =

        0;

    queue[rear++] =

        source;

    /*

     * BFS only needs to find the shortest

     * source-to-sink distance.

     *

     * Once sink is discovered, deeper levels

     * are irrelevant for this Dinic phase.

     */

    while (front < rear)

    {

        int32_t u =

            queue[front++];

        int32_t next_level =

            G->nodes[u].level + 1;

        struct Node *node =

            &G->nodes[u];

        for (int32_t i = 0;

             i < node->edge_count;

             ++i)

        {

            G->edge_scans++;

            struct Edge *edge =

                &node->edges[i];

            if (edge->residual_capacity <= 0)

                continue;

            int32_t v =

                edge->to_node;

            if (G->nodes[v].level != -1)

                continue;

            G->nodes[v].level =

                next_level;

            /*

             * Sink reached.

             *

             * No deeper level is required.

             */

            if (v == sink)

            {

                free(queue);

                G->bfs_phases++;

                return 1;
            }

            queue[rear++] =

                v;
        }
    }

    free(queue);

    G->bfs_phases++;

    return 0;
}

/* ============================================================

 * DFS / Blocking flow

 * ============================================================ */

static int64_t dfs(

    struct Graph *G,

    int32_t u,

    int64_t pushed,

    int32_t *current_edge)

{

    G->dfs_calls++;

    int32_t sink =

        G->sink;

    if (u == sink)

        return pushed;

    struct Node *node =

        &G->nodes[u];

    /*

     * Examine each outgoing edge at most once

     * per blocking-flow phase, using current_edge.

     */

    while (current_edge[u] <

           node->edge_count)

    {

        int32_t edge_index =

            current_edge[u];

        struct Edge *edge =

            &node->edges[edge_index];

        G->edge_scans++;

        if (edge->residual_capacity <= 0)

        {

            current_edge[u]++;

            continue;
        }

        int32_t v =

            edge->to_node;

        /*

         * Only move one level forward.

         */

        if (G->nodes[v].level !=

            node->level + 1)

        {

            current_edge[u]++;

            continue;
        }

        int64_t bottleneck =

            pushed < edge->residual_capacity

                ? pushed

                : edge->residual_capacity;

        int64_t flow =

            dfs(

                G,

                v,

                bottleneck,

                current_edge);

        if (flow > 0)

        {

            /*

             * Update forward residual capacity.

             */

            edge->residual_capacity -=

                flow;

            /*

             * Update reverse residual capacity.

             */

            G->nodes[v]

                .edges[edge->reverse_index]

                .residual_capacity +=

                flow;

            return flow;
        }

        /*

         * This edge can no longer contribute

         * useful flow through v.

         */

        current_edge[u]++;
    }

    /*

     * Dead-vertex optimization.

     *

     * No path from u to sink remains in this

     * level graph, so prevent future DFS calls

     * from entering u.

     */

    node->level = -1;

    return 0;
}

/* ============================================================

 * Dinic

 * ============================================================ */

static int64_t run_dinic(

    struct Graph *G)

{

    const int32_t n =

        G->node_count;

    const int32_t source =

        G->source;

    /*

     * Current-edge/current-arc pointer.

     *

     * One allocation for the entire algorithm.

     */

    int32_t *current_edge =

        calloc(

            (size_t)n,

            sizeof(int32_t));

    if (current_edge == NULL)

    {

        fprintf(

            stderr,

            "Memory allocation failed.\n");

        exit(EXIT_FAILURE);
    }

    int64_t max_flow =

        0;

    /*

     * --------------------------------------------------------

     * PHASES

     * --------------------------------------------------------

     *

     * Each iteration:

     *

     *   BFS -> level graph

     *   DFS -> blocking flow

     */

    while (bfs(G))

    {

        /*

         * Reset current arcs for this

         * blocking-flow computation.

         */

        memset(

            current_edge,

            0,

            (size_t)n *

                sizeof(int32_t));

        /*

         * Send blocking flow.

         */

        while (1)

        {

            int64_t pushed =

                dfs(

                    G,

                    source,

                    INT64_MAX,

                    current_edge);

            if (pushed == 0)

                break;

            max_flow +=

                pushed;

            G->augmentations++;
        }
    }

    free(current_edge);

    return max_flow;
}

/* ============================================================

 * Cleanup

 * ============================================================ */

static void free_graph(

    struct Graph *G)

{

    if (G == NULL)

        return;

    if (G->nodes != NULL)

    {

        for (int32_t i = 0;

             i < G->node_count;

             ++i)

        {

            free(

                G->nodes[i].edges);
        }

        free(G->nodes);
    }

    free(G);
}