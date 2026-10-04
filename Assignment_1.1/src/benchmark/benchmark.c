#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>

#include <inttypes.h>

#include <limits.h>

#include <math.h>

#include <stdbool.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <sys/resource.h>
#include <sys/stat.h>

#include <sys/types.h>

#include <sys/wait.h>

#include <unistd.h>

#define DEFAULT_RUNS 20

#define DEFAULT_WARMUPS 1

struct BenchResult

{

    int64_t nodes;

    int64_t edges;

    int64_t max_flow;

    int64_t execution_time_us;

    int64_t peak_memory_kb;

    int64_t bfs_phases;

    int64_t dfs_calls;

    int64_t augmentations;

    int64_t push_operations;

    int64_t relabel_operations;

    int64_t discharge_operations;

    int64_t edge_scans;

    int64_t global_relabels;

    int64_t gap_operations;

    bool have_nodes;

    bool have_edges;

    bool have_max_flow;

    bool have_execution_time_us;

    bool have_peak_memory_kb;

    bool have_bfs_phases;

    bool have_dfs_calls;

    bool have_augmentations;

    bool have_push_operations;

    bool have_relabel_operations;

    bool have_discharge_operations;

    bool have_edge_scans;
};

struct SummaryStats

{

    bool initialized;

    int64_t nodes;

    int64_t edges;

    int64_t max_flow;

    int64_t run_count;

    long double time_sum;

    long double time_sum_sq;

    int64_t time_min;

    int64_t time_max;

    long double mem_sum;

    long double mem_sum_sq;

    int64_t mem_min;

    int64_t mem_max;

    long double bfs_sum;

    long double dfs_sum;

    long double augmentations_sum;

    long double push_sum;

    long double relabel_sum;

    long double discharge_sum;

    long double edge_scans_sum;

    long double global_relabels_sum;

    long double gap_operations_sum;
};

static void print_usage(const char *prog)

{

    fprintf(stderr,

            "Usage: %s <dinic> <pushrelabel> <output.csv> [--runs N] [--warmup N] <dataset-folder>\n"

            "Defaults: --runs %d --warmup %d\n",

            prog, DEFAULT_RUNS, DEFAULT_WARMUPS);
}

static bool parse_nonnegative_int(const char *s, int *out)

{

    char *end = NULL;

    long value;

    if (s == NULL || *s == '\0')

    {

        return false;
    }

    errno = 0;

    value = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0' || value < 0 || value > INT_MAX)

    {

        return false;
    }

    *out = (int)value;

    return true;
}

static bool has_max_extension(const char *filename)

{

    const char *dot;

    if (filename == NULL || *filename == '\0')

    {

        return false;
    }

    dot = strrchr(filename, '.');

    return dot != NULL && strcmp(dot, ".max") == 0;
}

static int compare_string_pointers(const void *a, const void *b)

{

    const char *const *left = (const char *const *)a;

    const char *const *right = (const char *const *)b;

    return strcmp(*left, *right);
}

static char *join_path(const char *directory, const char *filename)

{

    size_t directory_len;

    size_t filename_len;

    bool need_separator;

    size_t total_len;

    char *path;

    if (directory == NULL || filename == NULL)

    {

        return NULL;
    }

    directory_len = strlen(directory);

    filename_len = strlen(filename);

    need_separator = directory_len > 0 &&

                     directory[directory_len - 1] != '/';

    if (directory_len > SIZE_MAX - filename_len - (need_separator ? 1U : 0U) - 1U)

    {

        return NULL;
    }

    total_len = directory_len + (need_separator ? 1U : 0U) + filename_len + 1U;

    path = malloc(total_len);

    if (path == NULL)

    {

        return NULL;
    }

    memcpy(path, directory, directory_len);

    if (need_separator)

    {

        path[directory_len] = '/';
    }

    memcpy(path + directory_len + (need_separator ? 1U : 0U), filename, filename_len);

    path[total_len - 1U] = '\0';

    return path;
}

static bool collect_max_datasets(const char *directory,

                                 char ***datasets_out,

                                 int *dataset_count_out)

{

    DIR *dir = NULL;

    struct dirent *entry;

    char **datasets = NULL;

    size_t capacity = 0;

    int dataset_count = 0;

    struct stat file_stat;

    if (directory == NULL || datasets_out == NULL || dataset_count_out == NULL)

    {

        return false;
    }

    dir = opendir(directory);

    if (dir == NULL)

    {

        fprintf(stderr, "benchmark: cannot open dataset folder '%s': %s\n",

                directory, strerror(errno));

        return false;
    }

    while ((entry = readdir(dir)) != NULL)

    {

        char *path;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)

        {

            continue;
        }

        if (!has_max_extension(entry->d_name))

        {

            continue;
        }

        path = join_path(directory, entry->d_name);

        if (path == NULL)

        {

            fprintf(stderr, "benchmark: failed to allocate path for '%s'\n", entry->d_name);

            goto error;
        }

        if (stat(path, &file_stat) != 0)

        {

            fprintf(stderr, "benchmark: cannot stat dataset '%s': %s\n",

                    path, strerror(errno));

            free(path);

            goto error;
        }

        if (!S_ISREG(file_stat.st_mode))

        {

            free(path);

            continue;
        }

        if ((size_t)dataset_count == capacity)

        {

            size_t new_capacity = (capacity == 0) ? 16U : capacity * 2U;

            char **grown;

            if (new_capacity < capacity ||

                new_capacity > SIZE_MAX / sizeof(*datasets))

            {

                fprintf(stderr, "benchmark: too many datasets in '%s'\n", directory);

                free(path);

                goto error;
            }

            grown = realloc(datasets, new_capacity * sizeof(*datasets));

            if (grown == NULL)

            {

                perror("benchmark: realloc");

                free(path);

                goto error;
            }

            datasets = grown;

            capacity = new_capacity;
        }

        datasets[dataset_count++] = path;
    }

    if (closedir(dir) != 0)

    {

        perror("benchmark: closedir");

        dir = NULL;

        goto error;
    }

    dir = NULL;

    if (dataset_count == 0)

    {

        fprintf(stderr, "benchmark: no regular .max dataset files found in '%s'\n", directory);

        free(datasets);

        return false;
    }

    qsort(datasets,

          (size_t)dataset_count,

          sizeof(*datasets),

          compare_string_pointers);

    *datasets_out = datasets;

    *dataset_count_out = dataset_count;

    return true;

error:

    if (dir != NULL)

    {

        closedir(dir);
    }

    for (int i = 0; i < dataset_count; ++i)

    {

        free(datasets[i]);
    }

    free(datasets);

    return false;
}

static bool parse_i64_value(const char *s, int64_t *out)

{

    char *end = NULL;

    long long value;

    if (s == NULL || *s == '\0')

    {

        return false;
    }

    errno = 0;

    value = strtoll(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0')

    {

        return false;
    }

    *out = (int64_t)value;

    return true;
}

static void reset_result(struct BenchResult *r)

{

    memset(r, 0, sizeof(*r));
}

static bool parse_bench_result_line(char *line, struct BenchResult *r)

{

    char *saveptr = NULL;

    char *token;

    if (strncmp(line, "BENCH_RESULT", 12) != 0)

    {

        return false;
    }

    reset_result(r);

    token = strtok_r(line, " \t\r\n", &saveptr);

    if (token == NULL || strcmp(token, "BENCH_RESULT") != 0)

    {

        return false;
    }

    while ((token = strtok_r(NULL, " \t\r\n", &saveptr)) != NULL)

    {

        char *eq = strchr(token, '=');

        const char *key;

        const char *value_text;

        int64_t value;

        bool known = false;

        if (eq == NULL)

        {

            continue;
        }

        *eq = '\0';

        key = token;

        value_text = eq + 1;

        /* Ignore unknown key=value fields so the algorithm output can grow without

           breaking the benchmark parser. All fields used by the CSV remain integer. */

        if (strcmp(key, "nodes") == 0 ||

            strcmp(key, "edges") == 0 ||

            strcmp(key, "max_flow") == 0 ||

            strcmp(key, "execution_time_us") == 0 ||

            strcmp(key, "peak_memory_kb") == 0 ||

            strcmp(key, "bfs_phases") == 0 ||

            strcmp(key, "dfs_calls") == 0 ||

            strcmp(key, "augmentations") == 0 ||

            strcmp(key, "push_operations") == 0 ||

            strcmp(key, "relabel_operations") == 0 ||

            strcmp(key, "discharge_operations") == 0 ||

            strcmp(key, "edge_scans") == 0 ||

            strcmp(key, "global_relabels") == 0 ||

            strcmp(key, "gap_operations") == 0)

        {

            known = true;
        }

        if (!known)

        {

            continue;
        }

        if (!parse_i64_value(value_text, &value))

        {

            fprintf(stderr, "benchmark: invalid BENCH_RESULT value for key '%s': '%s'\n",

                    key, value_text);

            return false;
        }

        if (strcmp(key, "nodes") == 0)

        {

            r->nodes = value;

            r->have_nodes = true;
        }

        else if (strcmp(key, "edges") == 0)

        {

            r->edges = value;

            r->have_edges = true;
        }

        else if (strcmp(key, "max_flow") == 0)

        {

            r->max_flow = value;

            r->have_max_flow = true;
        }

        else if (strcmp(key, "execution_time_us") == 0)

        {

            r->execution_time_us = value;

            r->have_execution_time_us = true;
        }

        else if (strcmp(key, "peak_memory_kb") == 0)

        {

            r->peak_memory_kb = value;

            r->have_peak_memory_kb = true;
        }

        else if (strcmp(key, "bfs_phases") == 0)

        {

            r->bfs_phases = value;

            r->have_bfs_phases = true;
        }

        else if (strcmp(key, "dfs_calls") == 0)

        {

            r->dfs_calls = value;

            r->have_dfs_calls = true;
        }

        else if (strcmp(key, "augmentations") == 0)

        {

            r->augmentations = value;

            r->have_augmentations = true;
        }

        else if (strcmp(key, "push_operations") == 0)

        {

            r->push_operations = value;

            r->have_push_operations = true;
        }

        else if (strcmp(key, "relabel_operations") == 0)

        {

            r->relabel_operations = value;

            r->have_relabel_operations = true;
        }

        else if (strcmp(key, "discharge_operations") == 0)

        {

            r->discharge_operations = value;

            r->have_discharge_operations = true;
        }

        else if (strcmp(key, "edge_scans") == 0)

        {

            r->edge_scans = value;

            r->have_edge_scans = true;
        }

        else if (strcmp(key, "global_relabels") == 0)

        {

            r->global_relabels = value;
        }

        else if (strcmp(key, "gap_operations") == 0)

        {

            r->gap_operations = value;
        }
    }

    return r->have_nodes &&

           r->have_edges &&

           r->have_max_flow &&

           r->have_execution_time_us &&

           r->have_bfs_phases &&

           r->have_dfs_calls &&

           r->have_augmentations &&

           r->have_push_operations &&

           r->have_relabel_operations &&

           r->have_discharge_operations &&

           r->have_edge_scans;
}

static void dump_capture(FILE *capture)

{

    char buffer[4096];

    size_t n;

    if (capture == NULL)

    {

        return;
    }

    fflush(capture);

    rewind(capture);

    fprintf(stderr, "----- child stdout -----\n");

    while ((n = fread(buffer, 1, sizeof(buffer), capture)) > 0)

    {

        (void)fwrite(buffer, 1, n, stderr);
    }

    fprintf(stderr, "----- end child stdout -----\n");
}

static bool extract_last_bench_result(FILE *capture, struct BenchResult *result)

{

    char *line = NULL;

    size_t capacity = 0;

    ssize_t length;

    bool found = false;

    fflush(capture);

    rewind(capture);

    while ((length = getline(&line, &capacity, capture)) != -1)

    {

        (void)length;

        if (strncmp(line, "BENCH_RESULT", 12) == 0)

        {

            char *copy = strdup(line);

            struct BenchResult parsed;

            if (copy == NULL)

            {

                free(line);

                return false;
            }

            if (!parse_bench_result_line(copy, &parsed))

            {

                free(copy);

                free(line);

                return false;
            }

            *result = parsed;

            found = true;

            free(copy);
        }
    }

    free(line);

    return found;
}

static int64_t peak_rss_kb(const struct rusage *usage)

{

#if defined(__APPLE__)

    /* macOS reports ru_maxrss in bytes. */

    return (int64_t)(usage->ru_maxrss / 1024L);

#else

    /* Linux reports ru_maxrss in KiB. */

    return (int64_t)usage->ru_maxrss;

#endif
}

static bool run_program(const char *program,

                        const char *dataset,

                        struct BenchResult *result)

{

    struct rusage usage;

    pid_t pid;

    int status;

    FILE *capture;

    int fd;

    capture = tmpfile();

    if (capture == NULL)

    {

        perror("benchmark: tmpfile");

        return false;
    }

    pid = fork();

    if (pid < 0)

    {

        perror("benchmark: fork");

        fclose(capture);

        return false;
    }

    if (pid == 0)

    {

        char *const child_argv[] = {

            (char *)program,

            (char *)dataset,

            NULL};

        fd = fileno(capture);

        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0)

        {

            dprintf(STDERR_FILENO, "benchmark: dup2 failed: %s\n", strerror(errno));

            _exit(126);
        }

        if (fclose(capture) != 0)

        {

            /* stdout is already duplicated; continue anyway. */
        }

        execvp(program, child_argv);

        dprintf(STDERR_FILENO, "benchmark: exec '%s' failed: %s\n",

                program, strerror(errno));

        _exit(127);
    }

    if (wait4(pid, &status, 0, &usage) < 0)

    {

        perror("benchmark: wait4");

        fclose(capture);

        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)

    {

        fprintf(stderr, "benchmark: '%s' failed for dataset '%s' (status=0x%x)\n",

                program, dataset, status);

        dump_capture(capture);

        fclose(capture);

        return false;
    }

    if (!extract_last_bench_result(capture, result))

    {

        fprintf(stderr, "benchmark: no valid BENCH_RESULT found from '%s' for dataset '%s'\n",

                program, dataset);

        dump_capture(capture);

        fclose(capture);

        return false;
    }

    /* The benchmark harness, not the child, is the authoritative source for peak RSS. */

    result->peak_memory_kb = peak_rss_kb(&usage);

    result->have_peak_memory_kb = true;

    fclose(capture);

    return true;
}

static void csv_write_quoted(FILE *out, const char *text)

{

    const char *p;

    fputc('"', out);

    for (p = text; *p != '\0'; ++p)

    {

        if (*p == '"')

        {

            fputc('"', out);
        }

        fputc(*p, out);
    }

    fputc('"', out);
}

static void write_raw_header(FILE *out)

{

    fprintf(out,

            "dataset,run,algorithm,nodes,edges,max_flow,execution_time_us,peak_memory_kb,"

            "bfs_phases,dfs_calls,augmentations,push_operations,relabel_operations,"

            "discharge_operations,edge_scans,global_relabels,gap_operations\n");
}

static void write_raw_row(FILE *out,

                          const char *dataset,

                          int run,

                          const char *algorithm,

                          const struct BenchResult *r)

{

    csv_write_quoted(out, dataset);

    fprintf(out, ",%d,", run);

    csv_write_quoted(out, algorithm);

    fprintf(out,

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            ",%" PRId64

            "\n",

            r->nodes,

            r->edges,

            r->max_flow,

            r->execution_time_us,

            r->peak_memory_kb,

            r->bfs_phases,

            r->dfs_calls,

            r->augmentations,

            r->push_operations,

            r->relabel_operations,

            r->discharge_operations,

            r->edge_scans,

            r->global_relabels,

            r->gap_operations);
}

static void summary_init(struct SummaryStats *s)

{

    memset(s, 0, sizeof(*s));

    s->time_min = INT64_MAX;

    s->time_max = INT64_MIN;

    s->mem_min = INT64_MAX;

    s->mem_max = INT64_MIN;
}

static void summary_add(struct SummaryStats *s, const struct BenchResult *r)

{

    if (!s->initialized)

    {

        s->initialized = true;

        s->nodes = r->nodes;

        s->edges = r->edges;

        s->max_flow = r->max_flow;
    }

    s->run_count++;

    s->time_sum += (long double)r->execution_time_us;

    s->time_sum_sq += (long double)r->execution_time_us * (long double)r->execution_time_us;

    if (r->execution_time_us < s->time_min)

        s->time_min = r->execution_time_us;

    if (r->execution_time_us > s->time_max)

        s->time_max = r->execution_time_us;

    s->mem_sum += (long double)r->peak_memory_kb;

    s->mem_sum_sq += (long double)r->peak_memory_kb * (long double)r->peak_memory_kb;

    if (r->peak_memory_kb < s->mem_min)

        s->mem_min = r->peak_memory_kb;

    if (r->peak_memory_kb > s->mem_max)

        s->mem_max = r->peak_memory_kb;

    s->bfs_sum += r->bfs_phases;

    s->dfs_sum += r->dfs_calls;

    s->augmentations_sum += r->augmentations;

    s->push_sum += r->push_operations;

    s->relabel_sum += r->relabel_operations;

    s->discharge_sum += r->discharge_operations;

    s->edge_scans_sum += r->edge_scans;

    s->global_relabels_sum += r->global_relabels;

    s->gap_operations_sum += r->gap_operations;
}

static long double sample_stddev(long double sum,

                                 long double sum_sq,

                                 int64_t n)

{

    long double variance;

    if (n <= 1)

    {

        return 0.0L;
    }

    variance = (sum_sq - (sum * sum) / (long double)n) / (long double)(n - 1);

    if (variance < 0.0L && variance > -1e-12L)

    {

        variance = 0.0L;
    }

    return variance > 0.0L ? sqrtl(variance) : 0.0L;
}

static void write_summary_header(FILE *out)

{

    fprintf(out,

            "dataset,algorithm,nodes,edges,max_flow,run_count,"

            "time_mean_us,time_stddev_us,time_min_us,time_max_us,"

            "memory_mean_kb,memory_stddev_kb,memory_min_kb,memory_max_kb,"

            "bfs_phases_mean,dfs_calls_mean,augmentations_mean,push_operations_mean,"

            "relabel_operations_mean,discharge_operations_mean,edge_scans_mean,"

            "global_relabels_mean,gap_operations_mean\n");
}

static void write_summary_row(FILE *out,

                              const char *dataset,

                              const char *algorithm,

                              const struct SummaryStats *s)

{

    long double time_mean = s->time_sum / (long double)s->run_count;

    long double time_sd = sample_stddev(s->time_sum, s->time_sum_sq, s->run_count);

    long double mem_mean = s->mem_sum / (long double)s->run_count;

    long double mem_sd = sample_stddev(s->mem_sum, s->mem_sum_sq, s->run_count);

    csv_write_quoted(out, dataset);

    fputc(',', out);

    csv_write_quoted(out, algorithm);

    fprintf(out,

            ",%" PRId64 /* nodes */

            ",%" PRId64 /* edges */

            ",%" PRId64 /* max_flow */

            ",%" PRId64 /* run_count */

            ",%.3Lf" /* time_mean_us */

            ",%.3Lf" /* time_stddev_us */

            ",%" PRId64 /* time_min_us */

            ",%" PRId64 /* time_max_us */

            ",%.3Lf" /* memory_mean_kb */

            ",%.3Lf" /* memory_stddev_kb */

            ",%" PRId64 /* memory_min_kb */

            ",%" PRId64 /* memory_max_kb */

            ",%.3Lf" /* bfs_phases_mean */

            ",%.3Lf" /* dfs_calls_mean */

            ",%.3Lf" /* augmentations_mean */

            ",%.3Lf" /* push_operations_mean */

            ",%.3Lf" /* relabel_operations_mean */

            ",%.3Lf" /* discharge_operations_mean */

            ",%.3Lf" /* edge_scans_mean */

            ",%.3Lf" /* global_relabels_mean */

            ",%.3Lf" /* gap_operations_mean */

            "\n",

            s->nodes,

            s->edges,

            s->max_flow,

            s->run_count,

            time_mean,

            time_sd,

            s->time_min,

            s->time_max,

            mem_mean,

            mem_sd,

            s->mem_min,

            s->mem_max,

            s->bfs_sum / (long double)s->run_count,

            s->dfs_sum / (long double)s->run_count,

            s->augmentations_sum / (long double)s->run_count,

            s->push_sum / (long double)s->run_count,

            s->relabel_sum / (long double)s->run_count,

            s->discharge_sum / (long double)s->run_count,

            s->edge_scans_sum / (long double)s->run_count,

            s->global_relabels_sum / (long double)s->run_count,

            s->gap_operations_sum / (long double)s->run_count);
}

static char *make_summary_filename(const char *raw_filename)

{

    const char *slash = strrchr(raw_filename, '/');

    const char *dot = strrchr(raw_filename, '.');

    size_t prefix_len;

    char *result;

    if (dot != NULL && (slash == NULL || dot > slash))

    {

        prefix_len = (size_t)(dot - raw_filename);
    }

    else

    {

        prefix_len = strlen(raw_filename);
    }

    result = malloc(prefix_len + strlen("_summary.csv") + 1);

    if (result == NULL)

    {

        return NULL;
    }

    memcpy(result, raw_filename, prefix_len);

    strcpy(result + prefix_len, "_summary.csv");

    return result;
}

static bool validate_result(const char *dataset,

                            const char *algorithm,

                            int64_t expected_nodes,

                            int64_t expected_edges,

                            int64_t expected_flow,

                            const struct BenchResult *r,

                            bool have_expected_flow)

{

    if (r->nodes != expected_nodes || r->edges != expected_edges)

    {

        fprintf(stderr,

                "benchmark: graph mismatch for dataset '%s' in %s: expected nodes=%" PRId64

                ", edges=%" PRId64 "; got nodes=%" PRId64 ", edges=%" PRId64 "\n",

                dataset, algorithm,

                expected_nodes, expected_edges,

                r->nodes, r->edges);

        return false;
    }

    if (have_expected_flow && r->max_flow != expected_flow)

    {

        fprintf(stderr,

                "benchmark: max-flow mismatch for dataset '%s' in %s: expected=%" PRId64

                ", got=%" PRId64 "\n",

                dataset, algorithm, expected_flow, r->max_flow);

        return false;
    }

    if (r->execution_time_us < 0 || r->peak_memory_kb < 0)

    {

        fprintf(stderr,

                "benchmark: invalid timing/memory result for dataset '%s' in %s\n",

                dataset, algorithm);

        return false;
    }

    return true;
}

int main(int argc, char **argv)

{

    const char *dinic_program;

    const char *pushrelabel_program;

    const char *raw_output;

    const char *dataset_folder = NULL;

    int runs = DEFAULT_RUNS;

    int warmups = DEFAULT_WARMUPS;

    int dataset_count = 0;

    char **datasets = NULL;

    struct SummaryStats *summary = NULL;

    FILE *raw = NULL;

    char *summary_filename = NULL;

    FILE *summary_file = NULL;

    int i;

    int dataset_index;

    int algorithm_index;

    int exit_code = EXIT_FAILURE;

    if (argc < 5)

    {

        print_usage(argv[0]);

        return EXIT_FAILURE;
    }

    dinic_program = argv[1];

    pushrelabel_program = argv[2];

    raw_output = argv[3];

    for (i = 4; i < argc; ++i)

    {

        if (strcmp(argv[i], "--runs") == 0 || strcmp(argv[i], "-r") == 0)

        {

            if (i + 1 >= argc || !parse_nonnegative_int(argv[i + 1], &runs) || runs <= 0)

            {

                fprintf(stderr, "benchmark: --runs requires a positive integer\n");

                goto cleanup;
            }

            ++i;
        }

        else if (strcmp(argv[i], "--warmup") == 0 || strcmp(argv[i], "-w") == 0)

        {

            if (i + 1 >= argc || !parse_nonnegative_int(argv[i + 1], &warmups))

            {

                fprintf(stderr, "benchmark: --warmup requires a non-negative integer\n");

                goto cleanup;
            }

            ++i;
        }

        else if (argv[i][0] == '-')

        {

            fprintf(stderr, "benchmark: unknown option '%s'\n", argv[i]);

            print_usage(argv[0]);

            goto cleanup;
        }

        else

        {

            if (dataset_folder != NULL)

            {

                fprintf(stderr,

                        "benchmark: only one dataset folder may be specified; unexpected argument '%s'\n",

                        argv[i]);

                print_usage(argv[0]);

                goto cleanup;
            }

            dataset_folder = argv[i];
        }
    }

    if (dataset_folder == NULL)

    {

        fprintf(stderr, "benchmark: a dataset folder is required\n");

        print_usage(argv[0]);

        goto cleanup;
    }

    if (!collect_max_datasets(dataset_folder, &datasets, &dataset_count))

    {

        goto cleanup;
    }

    summary = calloc((size_t)dataset_count * 2U, sizeof(*summary));

    if (summary == NULL)

    {

        perror("benchmark: calloc");

        goto cleanup;
    }

    for (i = 0; i < dataset_count * 2; ++i)

    {

        summary_init(&summary[i]);
    }

    raw = fopen(raw_output, "w");

    if (raw == NULL)

    {

        fprintf(stderr, "benchmark: cannot open '%s': %s\n", raw_output, strerror(errno));

        goto cleanup;
    }

    write_raw_header(raw);

    fprintf(stderr,

            "benchmark: dataset folder '%s'\n"

            "benchmark: found %d regular .max dataset file(s)\n"

            "benchmark: %d measured runs, %d warmup run(s) per algorithm\n",

            dataset_folder, dataset_count, runs, warmups);

    for (dataset_index = 0; dataset_index < dataset_count; ++dataset_index)

    {

        const char *dataset = datasets[dataset_index];

        int64_t expected_nodes = -1;

        int64_t expected_edges = -1;

        int64_t expected_flow = 0;

        bool have_expected_flow = false;

        fprintf(stderr, "\n[%d/%d] %s\n", dataset_index + 1, dataset_count, dataset);

        /* Warmups are deliberately excluded from the CSV. */

        for (i = 0; i < warmups; ++i)

        {

            int first = i & 1;

            int order[2] = {first, 1 - first};

            struct BenchResult warm_result;

            for (algorithm_index = 0; algorithm_index < 2; ++algorithm_index)

            {

                const int alg = order[algorithm_index];

                const char *program = (alg == 0) ? dinic_program : pushrelabel_program;

                if (!run_program(program, dataset, &warm_result))

                {

                    fprintf(stderr, "benchmark: warmup failed; aborting\n");

                    goto cleanup;
                }
            }
        }

        for (i = 1; i <= runs; ++i)

        {

            int first = (i - 1) & 1;

            int order[2] = {first, 1 - first};

            for (algorithm_index = 0; algorithm_index < 2; ++algorithm_index)

            {

                const int alg = order[algorithm_index];

                const char *algorithm_name = (alg == 0) ? "Dinic" : "Push-Relabel";

                const char *program = (alg == 0) ? dinic_program : pushrelabel_program;

                struct BenchResult result;

                if (!run_program(program, dataset, &result))

                {

                    fprintf(stderr, "benchmark: measured run failed; aborting\n");

                    goto cleanup;
                }

                if (expected_nodes < 0)

                {

                    expected_nodes = result.nodes;

                    expected_edges = result.edges;
                }

                if (!have_expected_flow)

                {

                    expected_flow = result.max_flow;

                    have_expected_flow = true;
                }

                if (!validate_result(dataset,

                                     algorithm_name,

                                     expected_nodes,

                                     expected_edges,

                                     expected_flow,

                                     &result,

                                     have_expected_flow))

                {

                    goto cleanup;
                }

                write_raw_row(raw, dataset, i, algorithm_name, &result);

                fflush(raw);

                summary_add(&summary[dataset_index * 2 + alg], &result);
            }

            fprintf(stderr, "  run %d/%d complete\n", i, runs);
        }
    }

    if (fflush(raw) != 0 || fclose(raw) != 0)

    {

        raw = NULL;

        fprintf(stderr, "benchmark: failed while closing raw output '%s'\n", raw_output);

        goto cleanup;
    }

    raw = NULL;

    summary_filename = make_summary_filename(raw_output);

    if (summary_filename == NULL)

    {

        fprintf(stderr, "benchmark: unable to allocate summary filename\n");

        goto cleanup;
    }

    summary_file = fopen(summary_filename, "w");

    if (summary_file == NULL)

    {

        fprintf(stderr, "benchmark: cannot open summary '%s': %s\n",

                summary_filename, strerror(errno));

        goto cleanup;
    }

    write_summary_header(summary_file);

    for (dataset_index = 0; dataset_index < dataset_count; ++dataset_index)

    {

        const char *dataset = datasets[dataset_index];

        const struct SummaryStats *dinic_stats = &summary[dataset_index * 2 + 0];

        const struct SummaryStats *pr_stats = &summary[dataset_index * 2 + 1];

        write_summary_row(summary_file, dataset, "Dinic", dinic_stats);

        write_summary_row(summary_file, dataset, "Push-Relabel", pr_stats);
    }

    if (fflush(summary_file) != 0 || fclose(summary_file) != 0)

    {

        summary_file = NULL;

        fprintf(stderr, "benchmark: failed while closing summary '%s'\n", summary_filename);

        goto cleanup;
    }

    summary_file = NULL;

    fprintf(stderr, "\nBenchmark complete.\n");

    fprintf(stderr, "Raw results:     %s\n", raw_output);

    fprintf(stderr, "Summary results: %s\n", summary_filename);

    exit_code = EXIT_SUCCESS;

cleanup:

    if (raw != NULL)

    {

        fclose(raw);
    }

    if (summary_file != NULL)

    {

        fclose(summary_file);
    }

    free(summary_filename);

    free(summary);

    if (datasets != NULL)

    {

        for (i = 0; i < dataset_count; ++i)

        {

            free(datasets[i]);
        }

        free(datasets);
    }

    return exit_code;
}