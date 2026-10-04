#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_RUNS 20
#define DEFAULT_WARMUPS 1
#define ALGORITHM_COUNT 2

struct BenchResult {
    char algorithm[64];

    int64_t nodes;
    int64_t edges;
    int64_t total_cost;
    int64_t execution_time_us;
    int64_t peak_memory_kb;

    /* Cycle-Canceling counters. */
    int64_t feasibility_bfs_phases;
    int64_t feasibility_augmentations;
    int64_t feasibility_edge_scans;
    int64_t negative_cycle_searches;
    int64_t negative_cycle_relaxations;
    int64_t negative_cycle_edge_scans;
    int64_t cycle_cancellations;
    int64_t cycle_edge_augmentations;

    /* Capacity-Scaling counters. */
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

    bool have_algorithm;
    bool have_nodes;
    bool have_edges;
    bool have_total_cost;
    bool have_execution_time_us;

    bool have_feasibility_bfs_phases;
    bool have_feasibility_augmentations;
    bool have_feasibility_edge_scans;
    bool have_negative_cycle_searches;
    bool have_negative_cycle_relaxations;
    bool have_negative_cycle_edge_scans;
    bool have_cycle_cancellations;
    bool have_cycle_edge_augmentations;

    bool have_scaling_phases;
    bool have_negative_arc_saturations;
    bool have_negative_arc_edge_scans;
    bool have_shortest_path_searches;
    bool have_shortest_path_successes;
    bool have_shortest_path_failures;
    bool have_shortest_path_edge_scans;
    bool have_dijkstra_relaxations;
    bool have_heap_insert_or_decrease;
    bool have_heap_pops;
    bool have_augmentations;
    bool have_path_edge_augmentations;

    bool have_peak_memory_kb;
};

struct Aggregate {
    int64_t count;
    int64_t nodes;
    int64_t edges;
    int64_t total_cost;

    long double time_sum;
    long double time_sum_sq;
    int64_t time_min;
    int64_t time_max;

    long double mem_sum;
    long double mem_sum_sq;
    int64_t mem_min;
    int64_t mem_max;

    long double feasibility_bfs_phases;
    long double feasibility_augmentations;
    long double feasibility_edge_scans;
    long double negative_cycle_searches;
    long double negative_cycle_relaxations;
    long double negative_cycle_edge_scans;
    long double cycle_cancellations;
    long double cycle_edge_augmentations;

    long double scaling_phases;
    long double negative_arc_saturations;
    long double negative_arc_edge_scans;
    long double shortest_path_searches;
    long double shortest_path_successes;
    long double shortest_path_failures;
    long double shortest_path_edge_scans;
    long double dijkstra_relaxations;
    long double heap_insert_or_decrease;
    long double heap_pops;
    long double augmentations;
    long double path_edge_augmentations;
};

static void print_usage(const char *program) {
    fprintf(stderr,
        "Usage: %s <cycle-canceling-program> <capacity-scaling-program> <raw.csv> [options] <dataset1> [dataset2 ...]\n"
        "Options:\n"
        "  --runs N, -r N       Number of measured runs (default: %d)\n"
        "  --warmup N, -w N     Warmup runs per dataset/algorithm (default: %d)\n",
        program, DEFAULT_RUNS, DEFAULT_WARMUPS);
}

static bool parse_positive_int(const char *text, int *value, bool allow_zero) {
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return false;
    if (parsed < 0 || parsed > INT32_MAX)
        return false;
    if (!allow_zero && parsed == 0)
        return false;

    *value = (int)parsed;
    return true;
}

static bool parse_i64(const char *text, int64_t *value) {
    char *end = NULL;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return false;

    *value = (int64_t)parsed;
    return true;
}

static bool set_i64_field(struct BenchResult *r,
                          const char *key,
                          const char *value) {
    int64_t v;
    if (!parse_i64(value, &v))
        return false;

#define SET_FIELD(name, flag) \
    if (strcmp(key, name) == 0) { r->flag##_value = v; r->flag = true; return true; }

    if (strcmp(key, "nodes") == 0) { r->nodes = v; r->have_nodes = true; return true; }
    if (strcmp(key, "edges") == 0) { r->edges = v; r->have_edges = true; return true; }
    if (strcmp(key, "total_cost") == 0) { r->total_cost = v; r->have_total_cost = true; return true; }
    if (strcmp(key, "execution_time_us") == 0) { r->execution_time_us = v; r->have_execution_time_us = true; return true; }

    if (strcmp(key, "feasibility_bfs_phases") == 0) { r->feasibility_bfs_phases = v; r->have_feasibility_bfs_phases = true; return true; }
    if (strcmp(key, "feasibility_augmentations") == 0) { r->feasibility_augmentations = v; r->have_feasibility_augmentations = true; return true; }
    if (strcmp(key, "feasibility_edge_scans") == 0) { r->feasibility_edge_scans = v; r->have_feasibility_edge_scans = true; return true; }
    if (strcmp(key, "negative_cycle_searches") == 0) { r->negative_cycle_searches = v; r->have_negative_cycle_searches = true; return true; }
    if (strcmp(key, "negative_cycle_relaxations") == 0) { r->negative_cycle_relaxations = v; r->have_negative_cycle_relaxations = true; return true; }
    if (strcmp(key, "negative_cycle_edge_scans") == 0) { r->negative_cycle_edge_scans = v; r->have_negative_cycle_edge_scans = true; return true; }
    if (strcmp(key, "cycle_cancellations") == 0) { r->cycle_cancellations = v; r->have_cycle_cancellations = true; return true; }
    if (strcmp(key, "cycle_edge_augmentations") == 0) { r->cycle_edge_augmentations = v; r->have_cycle_edge_augmentations = true; return true; }

    if (strcmp(key, "scaling_phases") == 0) { r->scaling_phases = v; r->have_scaling_phases = true; return true; }
    if (strcmp(key, "negative_arc_saturations") == 0) { r->negative_arc_saturations = v; r->have_negative_arc_saturations = true; return true; }
    if (strcmp(key, "negative_arc_edge_scans") == 0) { r->negative_arc_edge_scans = v; r->have_negative_arc_edge_scans = true; return true; }
    if (strcmp(key, "shortest_path_searches") == 0) { r->shortest_path_searches = v; r->have_shortest_path_searches = true; return true; }
    if (strcmp(key, "shortest_path_successes") == 0) { r->shortest_path_successes = v; r->have_shortest_path_successes = true; return true; }
    if (strcmp(key, "shortest_path_failures") == 0) { r->shortest_path_failures = v; r->have_shortest_path_failures = true; return true; }
    if (strcmp(key, "shortest_path_edge_scans") == 0) { r->shortest_path_edge_scans = v; r->have_shortest_path_edge_scans = true; return true; }
    if (strcmp(key, "dijkstra_relaxations") == 0) { r->dijkstra_relaxations = v; r->have_dijkstra_relaxations = true; return true; }
    if (strcmp(key, "heap_insert_or_decrease") == 0) { r->heap_insert_or_decrease = v; r->have_heap_insert_or_decrease = true; return true; }
    if (strcmp(key, "heap_pops") == 0) { r->heap_pops = v; r->have_heap_pops = true; return true; }
    if (strcmp(key, "augmentations") == 0) { r->augmentations = v; r->have_augmentations = true; return true; }
    if (strcmp(key, "path_edge_augmentations") == 0) { r->path_edge_augmentations = v; r->have_path_edge_augmentations = true; return true; }

#undef SET_FIELD
    /* Unknown numeric fields are intentionally ignored for forward compatibility. */
    return true;
}

static bool parse_bench_result_line(char *line, struct BenchResult *result) {
    char *saveptr = NULL;
    char *token;

    memset(result, 0, sizeof(*result));

    token = strtok_r(line, " \t\r\n", &saveptr);
    if (token == NULL || strcmp(token, "BENCH_RESULT") != 0)
        return false;

    while ((token = strtok_r(NULL, " \t\r\n", &saveptr)) != NULL) {
        char *equals = strchr(token, '=');
        if (equals == NULL || equals == token)
            return false;

        *equals = '\0';
        const char *key = token;
        const char *value = equals + 1;

        if (strcmp(key, "algorithm") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(result->algorithm))
                return false;
            strcpy(result->algorithm, value);
            result->have_algorithm = true;
        } else if (!set_i64_field(result, key, value)) {
            return false;
        }
    }

    return result->have_algorithm &&
           result->have_nodes &&
           result->have_edges &&
           result->have_total_cost &&
           result->have_execution_time_us;
}

static void dump_capture(FILE *capture) {
    char buffer[4096];
    size_t n;

    fflush(capture);
    rewind(capture);

    fprintf(stderr, "----- child stdout -----\n");
    while ((n = fread(buffer, 1, sizeof(buffer), capture)) > 0)
        (void)fwrite(buffer, 1, n, stderr);
    fprintf(stderr, "----- end child stdout -----\n");
}

static bool extract_last_bench_result(FILE *capture, struct BenchResult *result) {
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    bool found = false;

    fflush(capture);
    rewind(capture);

    while ((length = getline(&line, &capacity, capture)) != -1) {
        (void)length;
        if (strncmp(line, "BENCH_RESULT", strlen("BENCH_RESULT")) == 0) {
            struct BenchResult parsed;
            if (!parse_bench_result_line(line, &parsed)) {
                free(line);
                return false;
            }
            *result = parsed;
            found = true;
        }
    }

    free(line);
    return found;
}

static int64_t peak_rss_kb(const struct rusage *usage) {
#if defined(__APPLE__)
    return (int64_t)(usage->ru_maxrss / 1024L);
#else
    return (int64_t)usage->ru_maxrss;
#endif
}

static bool run_program(const char *program,
                        const char *dataset,
                        struct BenchResult *result) {
    FILE *capture;
    pid_t pid;
    int status;
    struct rusage usage;

    capture = tmpfile();
    if (capture == NULL) {
        fprintf(stderr, "benchmark: tmpfile failed: %s\n", strerror(errno));
        return false;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "benchmark: fork failed: %s\n", strerror(errno));
        fclose(capture);
        return false;
    }

    if (pid == 0) {
        int fd = fileno(capture);
        char *const child_argv[] = { (char *)program, (char *)dataset, NULL };

        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) {
            dprintf(STDERR_FILENO, "benchmark: stdout redirection failed: %s\n", strerror(errno));
            _exit(126);
        }

        fclose(capture);
        execvp(program, child_argv);

        dprintf(STDERR_FILENO,
                "benchmark: exec '%s' failed: %s\n",
                program, strerror(errno));
        _exit(127);
    }

    if (wait4(pid, &status, 0, &usage) < 0) {
        fprintf(stderr, "benchmark: wait4 failed: %s\n", strerror(errno));
        fclose(capture);
        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr,
                "benchmark: '%s' failed for '%s' (status=0x%x)\n",
                program, dataset, status);
        dump_capture(capture);
        fclose(capture);
        return false;
    }

    if (!extract_last_bench_result(capture, result)) {
        fprintf(stderr,
                "benchmark: no valid BENCH_RESULT from '%s' for '%s'\n",
                program, dataset);
        dump_capture(capture);
        fclose(capture);
        return false;
    }

    result->peak_memory_kb = peak_rss_kb(&usage);
    result->have_peak_memory_kb = true;

    fclose(capture);
    return true;
}

static void csv_write_quoted(FILE *out, const char *text) {
    const char *p;
    fputc('"', out);
    for (p = text; *p != '\0'; ++p) {
        if (*p == '"') fputc('"', out);
        fputc(*p, out);
    }
    fputc('"', out);
}

static void write_raw_header(FILE *out) {
    fprintf(out,
        "dataset,run,algorithm,nodes,edges,total_cost,execution_time_us,peak_memory_kb,"
        "feasibility_bfs_phases,feasibility_augmentations,feasibility_edge_scans,"
        "negative_cycle_searches,negative_cycle_relaxations,negative_cycle_edge_scans,"
        "cycle_cancellations,cycle_edge_augmentations,"
        "scaling_phases,negative_arc_saturations,negative_arc_edge_scans,"
        "shortest_path_searches,shortest_path_successes,shortest_path_failures,"
        "shortest_path_edge_scans,dijkstra_relaxations,heap_insert_or_decrease,heap_pops,"
        "augmentations,path_edge_augmentations\n");
}

static void csv_write_i64(FILE *out, int64_t value) {
    fprintf(out, "%" PRId64, value);
}

static void csv_write_ld(FILE *out, long double value) {
    fprintf(out, "%.3Lf", value);
}

static void write_raw_row(FILE *out,
                          const char *dataset,
                          int run,
                          const struct BenchResult *r) {
    csv_write_quoted(out, dataset);
    fprintf(out, ",%d,", run);
    csv_write_quoted(out, r->algorithm);

    fputc(',', out); csv_write_i64(out, r->nodes);
    fputc(',', out); csv_write_i64(out, r->edges);
    fputc(',', out); csv_write_i64(out, r->total_cost);
    fputc(',', out); csv_write_i64(out, r->execution_time_us);
    fputc(',', out); csv_write_i64(out, r->peak_memory_kb);

    fputc(',', out); csv_write_i64(out, r->feasibility_bfs_phases);
    fputc(',', out); csv_write_i64(out, r->feasibility_augmentations);
    fputc(',', out); csv_write_i64(out, r->feasibility_edge_scans);
    fputc(',', out); csv_write_i64(out, r->negative_cycle_searches);
    fputc(',', out); csv_write_i64(out, r->negative_cycle_relaxations);
    fputc(',', out); csv_write_i64(out, r->negative_cycle_edge_scans);
    fputc(',', out); csv_write_i64(out, r->cycle_cancellations);
    fputc(',', out); csv_write_i64(out, r->cycle_edge_augmentations);

    fputc(',', out); csv_write_i64(out, r->scaling_phases);
    fputc(',', out); csv_write_i64(out, r->negative_arc_saturations);
    fputc(',', out); csv_write_i64(out, r->negative_arc_edge_scans);
    fputc(',', out); csv_write_i64(out, r->shortest_path_searches);
    fputc(',', out); csv_write_i64(out, r->shortest_path_successes);
    fputc(',', out); csv_write_i64(out, r->shortest_path_failures);
    fputc(',', out); csv_write_i64(out, r->shortest_path_edge_scans);
    fputc(',', out); csv_write_i64(out, r->dijkstra_relaxations);
    fputc(',', out); csv_write_i64(out, r->heap_insert_or_decrease);
    fputc(',', out); csv_write_i64(out, r->heap_pops);
    fputc(',', out); csv_write_i64(out, r->augmentations);
    fputc(',', out); csv_write_i64(out, r->path_edge_augmentations);
    fputc('\n', out);
}

static void aggregate_init(struct Aggregate *a) {
    memset(a, 0, sizeof(*a));
    a->time_min = INT64_MAX;
    a->time_max = INT64_MIN;
    a->mem_min = INT64_MAX;
    a->mem_max = INT64_MIN;
}

static void aggregate_add(struct Aggregate *a, const struct BenchResult *r) {
    if (a->count == 0) {
        a->nodes = r->nodes;
        a->edges = r->edges;
        a->total_cost = r->total_cost;
    }

    ++a->count;
    a->time_sum += (long double)r->execution_time_us;
    a->time_sum_sq += (long double)r->execution_time_us * (long double)r->execution_time_us;
    if (r->execution_time_us < a->time_min) a->time_min = r->execution_time_us;
    if (r->execution_time_us > a->time_max) a->time_max = r->execution_time_us;

    a->mem_sum += (long double)r->peak_memory_kb;
    a->mem_sum_sq += (long double)r->peak_memory_kb * (long double)r->peak_memory_kb;
    if (r->peak_memory_kb < a->mem_min) a->mem_min = r->peak_memory_kb;
    if (r->peak_memory_kb > a->mem_max) a->mem_max = r->peak_memory_kb;

    a->feasibility_bfs_phases += r->feasibility_bfs_phases;
    a->feasibility_augmentations += r->feasibility_augmentations;
    a->feasibility_edge_scans += r->feasibility_edge_scans;
    a->negative_cycle_searches += r->negative_cycle_searches;
    a->negative_cycle_relaxations += r->negative_cycle_relaxations;
    a->negative_cycle_edge_scans += r->negative_cycle_edge_scans;
    a->cycle_cancellations += r->cycle_cancellations;
    a->cycle_edge_augmentations += r->cycle_edge_augmentations;

    a->scaling_phases += r->scaling_phases;
    a->negative_arc_saturations += r->negative_arc_saturations;
    a->negative_arc_edge_scans += r->negative_arc_edge_scans;
    a->shortest_path_searches += r->shortest_path_searches;
    a->shortest_path_successes += r->shortest_path_successes;
    a->shortest_path_failures += r->shortest_path_failures;
    a->shortest_path_edge_scans += r->shortest_path_edge_scans;
    a->dijkstra_relaxations += r->dijkstra_relaxations;
    a->heap_insert_or_decrease += r->heap_insert_or_decrease;
    a->heap_pops += r->heap_pops;
    a->augmentations += r->augmentations;
    a->path_edge_augmentations += r->path_edge_augmentations;
}

static long double sample_stddev(long double sum,
                                 long double sum_sq,
                                 int64_t n) {
    long double variance;

    if (n <= 1) return 0.0L;
    variance = (sum_sq - (sum * sum) / (long double)n) / (long double)(n - 1);
    if (variance < 0.0L && variance > -1e-9L) variance = 0.0L;
    return variance > 0.0L ? sqrtl(variance) : 0.0L;
}

static char *make_summary_filename(const char *raw_filename) {
    const char *slash = strrchr(raw_filename, '/');
    const char *dot = strrchr(raw_filename, '.');
    size_t prefix_len = strlen(raw_filename);
    char *result;

    if (dot != NULL && (slash == NULL || dot > slash))
        prefix_len = (size_t)(dot - raw_filename);

    result = malloc(prefix_len + strlen("_summary.csv") + 1);
    if (result == NULL) return NULL;

    memcpy(result, raw_filename, prefix_len);
    strcpy(result + prefix_len, "_summary.csv");
    return result;
}

static void write_summary_header(FILE *out) {
    fprintf(out,
        "dataset,algorithm,nodes,edges,total_cost,run_count,"
        "time_mean_us,time_stddev_us,time_min_us,time_max_us,"
        "memory_mean_kb,memory_stddev_kb,memory_min_kb,memory_max_kb,"
        "feasibility_bfs_phases_mean,feasibility_augmentations_mean,feasibility_edge_scans_mean,"
        "negative_cycle_searches_mean,negative_cycle_relaxations_mean,negative_cycle_edge_scans_mean,"
        "cycle_cancellations_mean,cycle_edge_augmentations_mean,"
        "scaling_phases_mean,negative_arc_saturations_mean,negative_arc_edge_scans_mean,"
        "shortest_path_searches_mean,shortest_path_successes_mean,shortest_path_failures_mean,"
        "shortest_path_edge_scans_mean,dijkstra_relaxations_mean,heap_insert_or_decrease_mean,"
        "heap_pops_mean,augmentations_mean,path_edge_augmentations_mean\n");
}

static void write_summary_row(FILE *out,
                              const char *dataset,
                              const char *algorithm,
                              const struct Aggregate *a) {
    long double tmean = a->time_sum / (long double)a->count;
    long double tsd = sample_stddev(a->time_sum, a->time_sum_sq, a->count);
    long double mmean = a->mem_sum / (long double)a->count;
    long double msd = sample_stddev(a->mem_sum, a->mem_sum_sq, a->count);
    long double n = (long double)a->count;

    csv_write_quoted(out, dataset);
    fputc(',', out);
    csv_write_quoted(out, algorithm);

    fputc(',', out); csv_write_i64(out, a->nodes);
    fputc(',', out); csv_write_i64(out, a->edges);
    fputc(',', out); csv_write_i64(out, a->total_cost);
    fputc(',', out); csv_write_i64(out, a->count);

    fputc(',', out); csv_write_ld(out, tmean);
    fputc(',', out); csv_write_ld(out, tsd);
    fputc(',', out); csv_write_i64(out, a->time_min);
    fputc(',', out); csv_write_i64(out, a->time_max);
    fputc(',', out); csv_write_ld(out, mmean);
    fputc(',', out); csv_write_ld(out, msd);
    fputc(',', out); csv_write_i64(out, a->mem_min);
    fputc(',', out); csv_write_i64(out, a->mem_max);

    fputc(',', out); csv_write_ld(out, a->feasibility_bfs_phases / n);
    fputc(',', out); csv_write_ld(out, a->feasibility_augmentations / n);
    fputc(',', out); csv_write_ld(out, a->feasibility_edge_scans / n);
    fputc(',', out); csv_write_ld(out, a->negative_cycle_searches / n);
    fputc(',', out); csv_write_ld(out, a->negative_cycle_relaxations / n);
    fputc(',', out); csv_write_ld(out, a->negative_cycle_edge_scans / n);
    fputc(',', out); csv_write_ld(out, a->cycle_cancellations / n);
    fputc(',', out); csv_write_ld(out, a->cycle_edge_augmentations / n);

    fputc(',', out); csv_write_ld(out, a->scaling_phases / n);
    fputc(',', out); csv_write_ld(out, a->negative_arc_saturations / n);
    fputc(',', out); csv_write_ld(out, a->negative_arc_edge_scans / n);
    fputc(',', out); csv_write_ld(out, a->shortest_path_searches / n);
    fputc(',', out); csv_write_ld(out, a->shortest_path_successes / n);
    fputc(',', out); csv_write_ld(out, a->shortest_path_failures / n);
    fputc(',', out); csv_write_ld(out, a->shortest_path_edge_scans / n);
    fputc(',', out); csv_write_ld(out, a->dijkstra_relaxations / n);
    fputc(',', out); csv_write_ld(out, a->heap_insert_or_decrease / n);
    fputc(',', out); csv_write_ld(out, a->heap_pops / n);
    fputc(',', out); csv_write_ld(out, a->augmentations / n);
    fputc(',', out); csv_write_ld(out, a->path_edge_augmentations / n);
    fputc('\n', out);
}

static bool validate_result(const char *dataset,
                            const char *expected_algorithm,
                            int64_t expected_nodes,
                            int64_t expected_edges,
                            int64_t expected_cost,
                            const struct BenchResult *r) {
    if (strcmp(r->algorithm, expected_algorithm) != 0) {
        fprintf(stderr,
                "benchmark: algorithm mismatch for '%s': expected '%s', got '%s'\n",
                dataset, expected_algorithm, r->algorithm);
        return false;
    }
    if (r->nodes != expected_nodes || r->edges != expected_edges) {
        fprintf(stderr,
                "benchmark: graph mismatch for '%s' in %s: expected nodes=%" PRId64
                ", edges=%" PRId64 "; got nodes=%" PRId64 ", edges=%" PRId64 "\n",
                dataset, expected_algorithm,
                expected_nodes, expected_edges, r->nodes, r->edges);
        return false;
    }
    if (r->total_cost != expected_cost) {
        fprintf(stderr,
                "benchmark: minimum-cost mismatch for '%s' in %s: expected=%" PRId64
                ", got=%" PRId64 "\n",
                dataset, expected_algorithm, expected_cost, r->total_cost);
        return false;
    }
    if (r->execution_time_us < 0 || r->peak_memory_kb < 0) {
        fprintf(stderr,
                "benchmark: invalid timing/memory result for '%s' in %s\n",
                dataset, expected_algorithm);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    const char *cycle_program;
    const char *capacity_program;
    const char *raw_output;
    const char *algorithm_names[ALGORITHM_COUNT] = {"Cycle-Canceling", "Capacity-Scaling"};
    const char *programs[ALGORITHM_COUNT];

    int runs = DEFAULT_RUNS;
    int warmups = DEFAULT_WARMUPS;
    int dataset_count = 0;
    const char **datasets = NULL;
    struct Aggregate *aggregates = NULL;
    FILE *raw = NULL;
    FILE *summary_file = NULL;
    char *summary_filename = NULL;
    int exit_code = EXIT_FAILURE;

    if (argc < 5) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    cycle_program = argv[1];
    capacity_program = argv[2];
    raw_output = argv[3];
    programs[0] = cycle_program;
    programs[1] = capacity_program;

    datasets = malloc((size_t)(argc - 4) * sizeof(*datasets));
    if (datasets == NULL) {
        fprintf(stderr, "benchmark: malloc failed\n");
        return EXIT_FAILURE;
    }

    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--runs") == 0 || strcmp(argv[i], "-r") == 0) {
            if (i + 1 >= argc || !parse_positive_int(argv[i + 1], &runs, false)) {
                fprintf(stderr, "benchmark: --runs requires a positive integer\n");
                goto cleanup;
            }
            ++i;
        } else if (strcmp(argv[i], "--warmup") == 0 || strcmp(argv[i], "-w") == 0) {
            if (i + 1 >= argc || !parse_positive_int(argv[i + 1], &warmups, true)) {
                fprintf(stderr, "benchmark: --warmup requires a non-negative integer\n");
                goto cleanup;
            }
            ++i;
        } else {
            datasets[dataset_count++] = argv[i];
        }
    }

    if (dataset_count == 0) {
        fprintf(stderr, "benchmark: at least one dataset is required\n");
        print_usage(argv[0]);
        goto cleanup;
    }

    aggregates = calloc((size_t)dataset_count * ALGORITHM_COUNT, sizeof(*aggregates));
    if (aggregates == NULL) {
        fprintf(stderr, "benchmark: calloc failed\n");
        goto cleanup;
    }
    for (int i = 0; i < dataset_count * ALGORITHM_COUNT; ++i)
        aggregate_init(&aggregates[i]);

    raw = fopen(raw_output, "w");
    if (raw == NULL) {
        fprintf(stderr, "benchmark: cannot open '%s': %s\n", raw_output, strerror(errno));
        goto cleanup;
    }
    write_raw_header(raw);

    fprintf(stderr,
            "benchmark: %d measured runs, %d warmup run(s), %d dataset(s)\n",
            runs, warmups, dataset_count);

    for (int dataset_index = 0; dataset_index < dataset_count; ++dataset_index) {
        const char *dataset = datasets[dataset_index];
        int64_t expected_nodes = -1;
        int64_t expected_edges = -1;
        int64_t expected_cost = INT64_MIN;
        bool have_reference = false;

        fprintf(stderr, "\n[%d/%d] %s\n", dataset_index + 1, dataset_count, dataset);

        /* Warmup. Excluded from results. */
        for (int w = 0; w < warmups; ++w) {
            int first = w & 1;
            for (int k = 0; k < ALGORITHM_COUNT; ++k) {
                int alg = (k == 0) ? first : 1 - first;
                struct BenchResult warm_result;
                if (!run_program(programs[alg], dataset, &warm_result)) {
                    fprintf(stderr, "benchmark: warmup failed; aborting\n");
                    goto cleanup;
                }
            }
        }

        /* Alternate algorithm order between measured runs. */
        for (int run = 1; run <= runs; ++run) {
            int first = (run - 1) & 1;

            for (int k = 0; k < ALGORITHM_COUNT; ++k) {
                int alg = (k == 0) ? first : 1 - first;
                struct BenchResult result;

                if (!run_program(programs[alg], dataset, &result)) {
                    fprintf(stderr, "benchmark: measured run failed; aborting\n");
                    goto cleanup;
                }

                if (!have_reference) {
                    expected_nodes = result.nodes;
                    expected_edges = result.edges;
                    expected_cost = result.total_cost;
                    have_reference = true;
                }

                if (!validate_result(dataset,
                                     algorithm_names[alg],
                                     expected_nodes,
                                     expected_edges,
                                     expected_cost,
                                     &result)) {
                    goto cleanup;
                }

                write_raw_row(raw, dataset, run, &result);
                aggregate_add(&aggregates[dataset_index * ALGORITHM_COUNT + alg], &result);
            }

            if (fflush(raw) != 0) {
                fprintf(stderr, "benchmark: failed writing '%s'\n", raw_output);
                goto cleanup;
            }

            fprintf(stderr, "  run %d/%d complete\n", run, runs);
        }
    }

    if (fclose(raw) != 0) {
        raw = NULL;
        fprintf(stderr, "benchmark: failed closing '%s'\n", raw_output);
        goto cleanup;
    }
    raw = NULL;

    summary_filename = make_summary_filename(raw_output);
    if (summary_filename == NULL) {
        fprintf(stderr, "benchmark: unable to create summary filename\n");
        goto cleanup;
    }

    summary_file = fopen(summary_filename, "w");
    if (summary_file == NULL) {
        fprintf(stderr,
                "benchmark: cannot open '%s': %s\n",
                summary_filename, strerror(errno));
        goto cleanup;
    }

    write_summary_header(summary_file);
    for (int dataset_index = 0; dataset_index < dataset_count; ++dataset_index) {
        for (int alg = 0; alg < ALGORITHM_COUNT; ++alg) {
            write_summary_row(
                summary_file,
                datasets[dataset_index],
                algorithm_names[alg],
                &aggregates[dataset_index * ALGORITHM_COUNT + alg]);
        }
    }

    if (fclose(summary_file) != 0) {
        summary_file = NULL;
        fprintf(stderr, "benchmark: failed closing '%s'\n", summary_filename);
        goto cleanup;
    }
    summary_file = NULL;

    fprintf(stderr, "\nBenchmark complete.\n");
    fprintf(stderr, "Raw results:     %s\n", raw_output);
    fprintf(stderr, "Summary results: %s\n", summary_filename);

    exit_code = EXIT_SUCCESS;

cleanup:
    if (raw != NULL) fclose(raw);
    if (summary_file != NULL) fclose(summary_file);
    free(summary_filename);
    free(aggregates);
    free(datasets);
    return exit_code;
}
