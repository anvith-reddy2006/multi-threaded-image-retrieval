/* =========================================================
   PERSISTENT WORKER POOL IMAGE RETRIEVAL  (barrier version)

   This is a variant of retrieval_threadpool.c. The ORIGINAL
   file creates a fresh set of worker threads for Phase 1
   (indexing), joins/destroys them, then creates a brand new
   set of threads for Phase 2 (search).

   This version creates the worker threads ONCE and reuses
   the exact same threads for both phases. A pthread_barrier
   is used to make every thread (and main) wait until ALL of
   Phase 1 is finished everywhere before ANY thread starts
   Phase 2 — this is required for correctness, because Phase 2
   reads the whole g_db array, including blocks decoded by
   OTHER threads, so no thread may start searching until every
   thread has finished indexing.

   Two separate bounded queues (one per phase) are used, so
   there is no need to reset/reinitialise a shared queue
   between phases — avoiding any reinit race condition.

   This file does NOT modify retrieval_threadpool.c,
   retrieval_mt.c, retrieval.c, common.c or common.h, and is
   built as a SEPARATE binary (retrieval_pool2) so none of the
   already-committed benchmark/verify results are affected.
   ========================================================= */

#include "../common.h"
#include <pthread.h>
#include <sys/resource.h>

/* =========================================================
   BOUNDED TASK QUEUE  (identical design to retrieval_threadpool.c)
   ========================================================= */

#define QUEUE_CAP 64

typedef struct {
    int   buf[QUEUE_CAP];
    int   front, rear, count;
    int   producer_done;
    pthread_mutex_t mtx;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} TaskQueue;

static void queue_init(TaskQueue *q)
{
    q->front = q->rear = q->count = 0;
    q->producer_done = 0;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full,  NULL);
}

static void queue_destroy(TaskQueue *q)
{
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static void queue_push(TaskQueue *q, int val)
{
    pthread_mutex_lock(&q->mtx);
    while (q->count == QUEUE_CAP)
        pthread_cond_wait(&q->not_full, &q->mtx);
    q->buf[q->rear] = val;
    q->rear = (q->rear + 1) % QUEUE_CAP;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
}

static int queue_pop(TaskQueue *q, int *val)
{
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0 && !q->producer_done)
        pthread_cond_wait(&q->not_empty, &q->mtx);
    if (q->count == 0 && q->producer_done) {
        pthread_mutex_unlock(&q->mtx);
        return 0;   /* done */
    }
    *val = q->buf[q->front];
    q->front = (q->front + 1) % QUEUE_CAP;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
    return 1;
}

static void queue_finish(TaskQueue *q)
{
    pthread_mutex_lock(&q->mtx);
    q->producer_done = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
}

/* =========================================================
   SHARED STATE
   ========================================================= */

static unsigned char *g_db;
static unsigned char *g_valid;
static unsigned char *g_query;
static char         **g_names;
static int            g_n;
static const char    *g_db_dir;

/* TWO separate queues — one per phase. Using two queues
   instead of resetting one shared queue avoids any race
   where a worker could start popping phase-2 tasks before
   the queue has actually been reinitialised for phase 2. */
static TaskQueue g_queue_index;
static TaskQueue g_queue_search;

/* Barrier: num_threads workers + the main thread.
   Guarantees Phase 1 (indexing) is 100% complete, for every
   image, before any thread is allowed to start Phase 2
   (search), since search reads the full shared g_db array. */
static pthread_barrier_t g_barrier;

/* =========================================================
   PER-WORKER DATA  (cache-line aligned to avoid false sharing
   — same technique already used in retrieval_threadpool.c)
   ========================================================= */

typedef struct __attribute__((aligned(64))) {
    int id;
    int images_indexed;
    int images_searched;
    Hit top[TOP_K];
    int topn;
    double t_index;
    double t_search;
} WorkerData;

/* =========================================================
   PERSISTENT WORKER — runs BOTH phases, created only once
   ========================================================= */

static void *worker(void *arg)
{
    WorkerData *w = arg;
    char path[512];

    /* ---------- Phase 1: indexing (PNG decode) ---------- */
    double t0 = now_sec();
    int block_id;
    while (queue_pop(&g_queue_index, &block_id)) {
        int lo = block_id * BLOCK_SIZE;
        int hi = lo + BLOCK_SIZE;
        if (hi > g_n) hi = g_n;
        for (int i = lo; i < hi; i++) {
            snprintf(path, sizeof(path), "%s/%s", g_db_dir, g_names[i]);
            if (load_gray32(path, g_db + i * FEATURE_SIZE))
                g_valid[i] = 1;
            w->images_indexed++;
        }
    }
    w->t_index = now_sec() - t0;

    /* ---- Wait here until every worker has finished Phase 1 ----
       This is the only synchronisation point between the two
       phases. No thread is created or destroyed here — the
       same worker just blocks until all siblings catch up. */
    pthread_barrier_wait(&g_barrier);

    /* ---------- Phase 2: search (distance computation) ---------- */
    t0 = now_sec();
    while (queue_pop(&g_queue_search, &block_id)) {
        int lo = block_id * BLOCK_SIZE;
        int hi = lo + BLOCK_SIZE;
        if (hi > g_n) hi = g_n;
        for (int i = lo; i < hi; i++) {
            if (!g_valid[i]) continue;
            Hit h = { sq_distance(g_query, g_db + i * FEATURE_SIZE), i };
            topk_insert(w->top, &w->topn, h);
            w->images_searched++;
        }
    }
    w->t_search = now_sec() - t0;

    return NULL;
}

/* =========================================================
   MAIN
   ========================================================= */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <query.png> [threads]\n", argv[0]);
        return 1;
    }
    int num_threads = (argc >= 3) ? atoi(argv[2]) : 4;
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
    int num_blocks = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;

    g_db    = calloc(n, FEATURE_SIZE);
    g_valid = calloc(n, 1);
    g_names = malloc(n * sizeof(char *));
    if (!g_db || !g_valid || !g_names) { perror("malloc"); return 1; }
    for (int i = 0; i < n; i++)
        g_names[i] = entries[i]->d_name;

    printf("=== PERSISTENT WORKER POOL IMAGE RETRIEVAL (barrier) ===\n");
    printf("Images found:   %d\n", n);
    printf("Worker threads: %d\n", num_threads);
    printf("Block size:     %d\n", BLOCK_SIZE);
    printf("Thread model:   SAME threads reused for both phases\n");
    printf("Phase handoff:  pthread_barrier_wait (no thread re-creation)\n");

    pthread_t   *tids  = malloc(num_threads * sizeof(pthread_t));
    WorkerData  *wdata = aligned_alloc(64, num_threads * sizeof(WorkerData));
    if (!tids || !wdata) { perror("malloc"); return 1; }
    memset(wdata, 0, num_threads * sizeof(WorkerData));

    queue_init(&g_queue_index);
    queue_init(&g_queue_search);
    /* barrier count = workers + main thread */
    pthread_barrier_init(&g_barrier, NULL, num_threads + 1);

    /* ==== T_total start ==== */
    double t_total_start = now_sec();

    /* ---- create all worker threads ONCE ---- */
    int created = 0;
    for (int i = 0; i < num_threads; i++) {
        wdata[i].id = i;
        if (pthread_create(&tids[i], NULL, worker, &wdata[i]) != 0) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
            exit(1);
        }
        created++;
    }
    if (created == 0) {
        fprintf(stderr, "Fatal: Could not create any threads.\n");
        exit(1);
    }

    /* ---- producer: push Phase-1 (index) blocks ---- */
    double t_index_start = now_sec();
    for (int b = 0; b < num_blocks; b++)
        queue_push(&g_queue_index, b);
    queue_finish(&g_queue_index);

    /* ---- producer: push Phase-2 (search) blocks ----
       Safe to do now even though workers may still be busy
       with Phase 1 — this is a SEPARATE queue, and workers
       will not touch it until they pass the barrier below. */
    for (int b = 0; b < num_blocks; b++)
        queue_push(&g_queue_search, b);
    queue_finish(&g_queue_search);

    /* ---- main also waits at the barrier ----
       This blocks until every worker has finished Phase 1,
       which lets us measure T_index accurately and guarantees
       correctness (no worker reads g_db before all writes to
       it are complete). */
    pthread_barrier_wait(&g_barrier);
    double t_index_end = now_sec();
    double t_search_start = t_index_end;

    /* ---- join: workers finish Phase 2 on their own ---- */
    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);
    double t_search_end = now_sec();

    /* ---- merge per-worker top-k ---- */
    Hit final_top[TOP_K];
    int final_n = 0;
    for (int i = 0; i < created; i++)
        topk_merge(final_top, &final_n, wdata[i].top, wdata[i].topn);

    double t_total_end = now_sec();

    /* ---- context switches ---- */
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    /* ---- results ---- */
    int total_indexed = 0, valid_count = 0;
    for (int i = 0; i < created; i++) total_indexed += wdata[i].images_indexed;
    for (int i = 0; i < n; i++) if (g_valid[i]) valid_count++;
    printf("\nTotal indexed: %d  Valid: %d  Failed: %d\n",
           total_indexed, valid_count, total_indexed - valid_count);

    printf("\nPer-worker timing (same thread, both phases):\n");
    for (int i = 0; i < num_threads; i++)
        printf("  Worker %d: index=%.6f s  search=%.6f s\n",
               i, wdata[i].t_index, wdata[i].t_search);

    printf("\nTop %d Similar Images:\n", TOP_K);
    for (int i = 0; i < final_n; i++) {
        printf("%d. %s/%s | Distance^2: %d\n",
               i + 1, g_db_dir, g_names[final_top[i].idx], final_top[i].dist);
    }

    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);
    printf("Context switches: %ld voluntary, %ld involuntary\n",
           ru.ru_nvcsw, ru.ru_nivcsw);

    /* ---- cleanup ---- */
    pthread_barrier_destroy(&g_barrier);
    queue_destroy(&g_queue_index);
    queue_destroy(&g_queue_search);
    for (int i = 0; i < n; i++) free(entries[i]);
    free(entries);
    free(g_db); free(g_valid); free(g_names);
    free(tids); free(wdata);
    return 0;
}
