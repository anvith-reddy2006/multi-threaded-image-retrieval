/* =========================================================
   STATIC MULTITHREADED IMAGE RETRIEVAL

   Parallel indexing  (PNG decode) with static partitioning.
   Parallel search    with per-thread local top-k, then merge.
   ========================================================= */

#include "../common.h"
#include <pthread.h>
#include <sys/resource.h>

/* ----- shared state ----- */
static unsigned char *g_db;
static unsigned char *g_valid;
static unsigned char *g_query;
static char         **g_names;
static int            g_n;
static const char    *g_db_dir;

/* ----- per-thread data ----- */
typedef struct __attribute__((aligned(64))) {
    int id;
    int start, end;          /* half-open range             */
    int loaded, failed;      /* indexing counters           */
    Hit top[TOP_K];
    int topn;
    double t_index, t_search;
} ThreadArg;  /* align attribute on the struct ensures 128 bytes size */

/* ----- index worker (PNG decode) ----- */
static void *index_worker(void *arg)
{
    ThreadArg *a = arg;
    char path[512];
    double t0 = now_sec();
    for (int i = a->start; i < a->end; i++) {
        snprintf(path, sizeof(path), "%s/%s", g_db_dir, g_names[i]);
        if (load_gray32(path, g_db + i * FEATURE_SIZE)) {
            g_valid[i] = 1;
            a->loaded++;
        } else {
            a->failed++;
        }
    }
    a->t_index = now_sec() - t0;
    return NULL;
}

/* ----- search worker (distance computation) ----- */
static void *search_worker(void *arg)
{
    ThreadArg *a = arg;
    a->topn = 0;
    double t0 = now_sec();
    for (int i = a->start; i < a->end; i++) {
        if (!g_valid[i]) continue;
        Hit h = { sq_distance(g_query, g_db + i * FEATURE_SIZE), i };
        topk_insert(a->top, &a->topn, h);
    }
    a->t_search = now_sec() - t0;
    return NULL;
}

/* ----- static partition helper ----- */
static void partition(int total, int nthreads, ThreadArg *args)
{
    int base = total / nthreads;
    int rem  = total % nthreads;
    int off  = 0;
    for (int i = 0; i < nthreads; i++) {
        args[i].id     = i;
        args[i].start  = off;
        args[i].end    = off + base + (i < rem ? 1 : 0);
        args[i].loaded = 0;
        args[i].failed = 0;
        args[i].topn   = 0;
        off = args[i].end;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <query.png> <num_threads>\n", argv[0]);
        return 1;
    }
    int num_threads = atoi(argv[2]);
    if (num_threads < 1 || num_threads > 256) {
        fprintf(stderr, "Thread count must be 1..256\n");
        return 1;
    }

    g_db_dir = getenv("IMG_DB_DIR") ? getenv("IMG_DB_DIR") : "dataset/train";

    /* ---- load query ---- */
    unsigned char query[FEATURE_SIZE];
    if (!load_gray32(argv[1], query)) {
        fprintf(stderr, "Cannot load query: %s\n", argv[1]);
        return 1;
    }
    g_query = query;

    /* ---- scan directory ---- */
    struct dirent **entries;
    int n = scan_png_dir(g_db_dir, &entries);
    if (n == 0) {
        fprintf(stderr, "No .png files in %s\n", g_db_dir);
        return 1;
    }
    g_n = n;

    g_db    = calloc(n, FEATURE_SIZE);
    g_valid = calloc(n, 1);
    g_names = malloc(n * sizeof(char *));
    if (!g_db || !g_valid || !g_names) { perror("malloc"); return 1; }
    for (int i = 0; i < n; i++)
        g_names[i] = entries[i]->d_name;

    printf("=== STATIC MULTITHREADED IMAGE RETRIEVAL ===\n");
    printf("Images found: %d\n", n);
    printf("Threads:      %d\n", num_threads);

    pthread_t *tids = malloc(num_threads * sizeof(pthread_t));
    ThreadArg *args = aligned_alloc(64, num_threads * sizeof(ThreadArg));
    if (!tids || !args) { perror("malloc"); return 1; }
    memset(args, 0, num_threads * sizeof(ThreadArg));

    /* ==== T_total start ==== */
    double t_total_start = now_sec();

    /* ---- T_index: parallel PNG decode ---- */
    partition(n, num_threads, args);
    double t_index_start = now_sec();
    int created = 0;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&tids[i], NULL, index_worker, &args[i]) != 0) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
            exit(1);
        }
        created++;
    }
    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);
    double t_index_end = now_sec();

    int total_loaded = 0, total_failed = 0;
    for (int i = 0; i < created; i++) {
        total_loaded += args[i].loaded;
        total_failed += args[i].failed;
    }

    /* ---- T_search: parallel distance computation ---- */
    partition(n, num_threads, args);
    double t_search_start = now_sec();
    created = 0;
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&tids[i], NULL, search_worker, &args[i]) != 0) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
            exit(1);
        }
        created++;
    }
    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);

    /* merge per-thread top-k */
    Hit final_top[TOP_K];
    int final_n = 0;
    for (int i = 0; i < created; i++)
        topk_merge(final_top, &final_n, args[i].top, args[i].topn);
    double t_search_end = now_sec();

    double t_total_end = now_sec();

    /* ---- context switches ---- */
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    /* ---- results ---- */
    printf("Loaded: %d  Failed: %d\n", total_loaded, total_failed);
    printf("\nTop %d Similar Images:\n", TOP_K);
    for (int i = 0; i < final_n; i++) {
        printf("%d. %s/%s | Distance^2: %d\n",
               i + 1, g_db_dir, g_names[final_top[i].idx], final_top[i].dist);
    }

    printf("\nPer-thread index time:\n");
    for (int i = 0; i < num_threads; i++)
        printf("  Thread %d: %.6f s  (images %d..%d)\n",
               i, args[i].t_index, args[i].start, args[i].end - 1);

    printf("Per-thread search time:\n");
    for (int i = 0; i < num_threads; i++)
        printf("  Thread %d: %.6f s\n", i, args[i].t_search);

    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);
    printf("Context switches: %ld voluntary, %ld involuntary\n",
           ru.ru_nvcsw, ru.ru_nivcsw);

    /* ---- cleanup ---- */
    for (int i = 0; i < n; i++) free(entries[i]);
    free(entries);
    free(g_db); free(g_valid); free(g_names);
    free(tids); free(args);
    return 0;
}
