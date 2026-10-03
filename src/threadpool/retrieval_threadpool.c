/* =========================================================
   PHASE-BASED WORKER POOL IMAGE RETRIEVAL

   Phase 1 – Parallel indexing:  producer pushes BLOCK_SIZE
             image-block IDs; workers decode PNG blocks.
   Phase 2 – Parallel search:    producer pushes block IDs;
             workers compute distances with local top-k.
   ========================================================= */

#include "../common.h"
#include <pthread.h>
#include <sys/resource.h>

/* =========================================================
   BOUNDED TASK QUEUE  (condvar-based producer-consumer)
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
static TaskQueue      g_queue;

/* =========================================================
   PER-WORKER DATA  (cache-line aligned to avoid false sharing)
   ========================================================= */

typedef struct __attribute__((aligned(64))) {
    int id;
    int images_done;
    Hit top[TOP_K];
    int topn;
    double elapsed;
} WorkerData;

/* =========================================================
   INDEX WORKER — decode a block of PNGs
   ========================================================= */

static void *index_worker(void *arg)
{
    WorkerData *w = arg;
    w->images_done = 0;
    char path[512];
    double t0 = now_sec();
    int block_id;
    while (queue_pop(&g_queue, &block_id)) {
        int lo = block_id * BLOCK_SIZE;
        int hi = lo + BLOCK_SIZE;
        if (hi > g_n) hi = g_n;
        for (int i = lo; i < hi; i++) {
            snprintf(path, sizeof(path), "dataset/train/%s", g_names[i]);
            if (load_gray32(path, g_db + i * FEATURE_SIZE))
                g_valid[i] = 1;
            w->images_done++;
        }
    }
    w->elapsed = now_sec() - t0;
    return NULL;
}

/* =========================================================
   SEARCH WORKER — compute distances for a block, local top-k
   ========================================================= */

static void *search_worker(void *arg)
{
    WorkerData *w = arg;
    w->images_done = 0;
    w->topn = 0;
    double t0 = now_sec();
    int block_id;
    while (queue_pop(&g_queue, &block_id)) {
        int lo = block_id * BLOCK_SIZE;
        int hi = lo + BLOCK_SIZE;
        if (hi > g_n) hi = g_n;
        for (int i = lo; i < hi; i++) {
            if (!g_valid[i]) continue;
            Hit h = { sq_distance(g_query, g_db + i * FEATURE_SIZE), i };
            topk_insert(w->top, &w->topn, h);
            w->images_done++;
        }
    }
    w->elapsed = now_sec() - t0;
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

    /* ---- load query ---- */
    unsigned char query[FEATURE_SIZE];
    if (!load_gray32(argv[1], query)) {
        fprintf(stderr, "Cannot load query: %s\n", argv[1]);
        return 1;
    }
    g_query = query;
    const char *db_dir = getenv("IMG_DB_DIR") ? getenv("IMG_DB_DIR") : "dataset/train";

    /* ---- scan directory ---- */
    struct dirent **entries;
    int n = scan_png_dir(db_dir, &entries);
    if (n == 0) {
        fprintf(stderr, "No .png files in dataset/train\n");
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

    printf("=== PHASE-BASED WORKER POOL IMAGE RETRIEVAL ===\n");
    printf("Images found:   %d\n", n);
    printf("Worker threads: %d\n", num_threads);
    printf("Block size:     %d\n", BLOCK_SIZE);
    printf("Scheduling:     Dynamic (producer-consumer)\n");
    printf("Synchronization: Mutex + Condition Variables\n");

    pthread_t *tids = malloc(num_threads * sizeof(pthread_t));
    WorkerData *wdata = aligned_alloc(64, num_threads * sizeof(WorkerData));
    if (!tids || !wdata) { perror("malloc"); return 1; }
    memset(wdata, 0, num_threads * sizeof(WorkerData));

    /* ==== T_total start ==== */
    double t_total_start = now_sec();

    /* ================================================
       PHASE 1: Parallel indexing (PNG decode)
       ================================================ */
    queue_init(&g_queue);
    double t_index_start = now_sec();

    /* create workers */
    int created = 0;
    for (int i = 0; i < num_threads; i++) {
        wdata[i].id = i;
        if (pthread_create(&tids[i], NULL, index_worker, &wdata[i]) != 0) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
            exit(1);
        }
        created++;
    }
    if (created == 0) {
        fprintf(stderr, "Fatal: Could not create any threads.\n");
        exit(1);
    }
    /* producer: push block IDs */
    for (int b = 0; b < num_blocks; b++)
        queue_push(&g_queue, b);
    queue_finish(&g_queue);

    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);
    double t_index_end = now_sec();
    queue_destroy(&g_queue);

    printf("\nIndex phase — per-worker images decoded:\n");
    int total_loaded = 0;
    for (int i = 0; i < created; i++) {
        printf("  Worker %d: %d images (%.6f s)\n",
               i, wdata[i].images_done, wdata[i].elapsed);
        total_loaded += wdata[i].images_done;
    }
    int valid_count = 0;
    for (int i = 0; i < n; i++)
        if (g_valid[i]) valid_count++;
    printf("Total decoded: %d  Valid: %d  Failed: %d\n",
           total_loaded, valid_count, total_loaded - valid_count);

    /* ================================================
       PHASE 2: Parallel search (distance computation)
       ================================================ */
    queue_init(&g_queue);
    double t_search_start = now_sec();

    created = 0;
    for (int i = 0; i < num_threads; i++) {
        wdata[i].id = i;
        wdata[i].images_done = 0;
        wdata[i].topn = 0;
        wdata[i].elapsed = 0;
        if (pthread_create(&tids[i], NULL, search_worker, &wdata[i]) != 0) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
            exit(1);
        }
        created++;
    }
    if (created == 0) {
        fprintf(stderr, "Fatal: Could not create any threads.\n");
        exit(1);
    }
    for (int b = 0; b < num_blocks; b++)
        queue_push(&g_queue, b);
    queue_finish(&g_queue);

    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);

    /* merge per-worker top-k */
    Hit final_top[TOP_K];
    int final_n = 0;
    for (int i = 0; i < created; i++)
        topk_merge(final_top, &final_n, wdata[i].top, wdata[i].topn);
    double t_search_end = now_sec();
    queue_destroy(&g_queue);

    double t_total_end = now_sec();

    /* ---- context switches ---- */
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    /* ---- results ---- */
    printf("\nSearch phase — per-worker images compared:\n");
    for (int i = 0; i < num_threads; i++)
        printf("  Worker %d: %d images (%.6f s)\n",
               i, wdata[i].images_done, wdata[i].elapsed);

    printf("\nTop %d Similar Images:\n", TOP_K);
    for (int i = 0; i < final_n; i++) {
        int orig_idx = final_top[i].idx % g_n;
        printf("%d. dataset/train/%s | Distance^2: %d\n",
               i + 1, g_names[orig_idx], final_top[i].dist);
    }

    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);
    printf("Context switches: %ld voluntary, %ld involuntary\n",
           ru.ru_nvcsw, ru.ru_nivcsw);

    /* ---- cleanup ---- */
    for (int i = 0; i < n; i++) free(entries[i]);
    free(entries);
    free(g_db); free(g_valid); free(g_names);
    free(tids); free(wdata);
    return 0;
}
