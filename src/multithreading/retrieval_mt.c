#include "../common.h"
#include <pthread.h>
#include <sys/resource.h>

static int           *g_db;
static unsigned char *g_valid;
static int           *g_query_hist;
static char         **g_names;
static int            g_n;
static int            g_search_repeat = 1;

typedef struct {
    int id, start, end, loaded, failed;
    Hit top[TOP_K];
    int topn;
    double t_index, t_search;
} ThreadArg __attribute__((aligned(64)));

static void *index_worker(void *arg)
{
    ThreadArg *a = arg;
    char path[512];
    unsigned char tmp_pixels[FEATURE_SIZE];
    double t0 = now_sec();
    for (int i = a->start; i < a->end; i++) {
        snprintf(path, sizeof(path), "dataset/train/%s", g_names[i]);
        if (load_gray32(path, tmp_pixels)) {
            compute_histogram(tmp_pixels, g_db + i * HIST_BINS);
            g_valid[i] = 1;
            a->loaded++;
        } else { a->failed++; }
    }
    a->t_index = now_sec() - t0;
    return NULL;
}

static void *search_worker(void *arg)
{
    ThreadArg *a = arg;
    a->topn = 0;
    double t0 = now_sec();
    for (int rep = 0; rep < g_search_repeat; rep++) {
        for (int i = a->start; i < a->end; i++) {
            if (!g_valid[i]) continue;
            Hit h = { hist_distance(g_query_hist, g_db + i * HIST_BINS), i };
            topk_insert(a->top, &a->topn, h);
        }
    }
    a->t_search = now_sec() - t0;
    return NULL;
}

static void partition(int total, int nthreads, ThreadArg *args)
{
    int base = total / nthreads, rem = total % nthreads, off = 0;
    for (int i = 0; i < nthreads; i++) {
        args[i].id = i; args[i].start = off;
        args[i].end = off + base + (i < rem ? 1 : 0);
        args[i].loaded = 0; args[i].failed = 0; args[i].topn = 0;
        off = args[i].end;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <query.png> <threads>\n", argv[0]);
        return 1;
    }
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <query.png> <threads>\n", argv[0]);
        return 1;
    }
    int num_threads = atoi(argv[2]);
    if (num_threads < 1) {
        fprintf(stderr, "Usage: %s <query.png> <threads>\n", argv[0]);
        return 1;
    }

    unsigned char query_pixels[FEATURE_SIZE];
    if (!load_gray32(argv[1], query_pixels)) return 1;
    int query_hist[HIST_BINS];
    compute_histogram(query_pixels, query_hist);
    g_query_hist = query_hist;

    struct dirent **entries;
    int total_entries = scan_png_dir("dataset/train", &entries);
    if (total_entries == 0) { fprintf(stderr, "No .png files\n"); return 1; }

    int n = total_entries;
    const char *max_img_env = getenv("MAX_IMAGES");
    if (max_img_env) {
        int max_img = atoi(max_img_env);
        if (max_img > 0 && max_img < n) {
            n = max_img;
        }
    }
    g_n = n;

    const char *sr_env = getenv("SEARCH_REPEAT");
    if (sr_env) {
        int sr = atoi(sr_env);
        if (sr > 1) g_search_repeat = sr;
    }

    g_db = calloc(n, HIST_BINS * sizeof(int));
    g_valid = calloc(n, 1);
    g_names = malloc(n * sizeof(char *));
    for (int i = 0; i < n; i++) g_names[i] = entries[i]->d_name;

    printf("=== STATIC MT HISTOGRAM RETRIEVAL ===\n");
    printf("Images found: %d\n", n);
    printf("Threads: %d\n", num_threads);

    pthread_t *tids = malloc(num_threads * sizeof(pthread_t));
    ThreadArg *args = calloc(num_threads, sizeof(ThreadArg));

    double t_total_start = now_sec();

    partition(n, num_threads, args);
    double t_index_start = now_sec();
    for (int i = 0; i < num_threads; i++) pthread_create(&tids[i], NULL, index_worker, &args[i]);
    for (int i = 0; i < num_threads; i++) pthread_join(tids[i], NULL);
    double t_index_end = now_sec();

    partition(n, num_threads, args);
    double t_search_start = now_sec();
    for (int i = 0; i < num_threads; i++) pthread_create(&tids[i], NULL, search_worker, &args[i]);
    for (int i = 0; i < num_threads; i++) pthread_join(tids[i], NULL);
    
    Hit final_top[TOP_K];
    int final_n = 0;
    for (int i = 0; i < num_threads; i++) topk_merge(final_top, &final_n, args[i].top, args[i].topn);
    double t_search_end = now_sec();
    double t_total_end = now_sec();

    struct rusage ru; getrusage(RUSAGE_SELF, &ru);

    printf("\nTop %d Similar Images (Euclidean Histogram Distance):\n", TOP_K);
    for (int i = 0; i < final_n; i++) {
        printf("%d. dataset/train/%s | Euclidean Dist: %d\n", i + 1, g_names[final_top[i].idx], final_top[i].dist);
    }
    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);
    printf("Context switches: %ld voluntary, %ld involuntary\n", ru.ru_nvcsw, ru.ru_nivcsw);

    for (int i = 0; i < total_entries; i++) {
        free(entries[i]);
    }
    free(entries);
    free(g_db);
    free(g_valid);
    free(g_names);
    free(tids);
    free(args);
    return 0;
}
