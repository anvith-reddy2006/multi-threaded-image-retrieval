#include "../common.h"
#include <pthread.h>
#include <sys/resource.h>

#define QUEUE_CAP 64
typedef struct {
    int buf[QUEUE_CAP], front, rear, count, producer_done;
    pthread_mutex_t mtx; pthread_cond_t not_empty, not_full;
} TaskQueue;

static void queue_init(TaskQueue *q) { q->front = q->rear = q->count = q->producer_done = 0; pthread_mutex_init(&q->mtx, NULL); pthread_cond_init(&q->not_empty, NULL); pthread_cond_init(&q->not_full, NULL); }
static void queue_destroy(TaskQueue *q) { pthread_mutex_destroy(&q->mtx); pthread_cond_destroy(&q->not_empty); pthread_cond_destroy(&q->not_full); }
static void queue_push(TaskQueue *q, int val) { pthread_mutex_lock(&q->mtx); while(q->count == QUEUE_CAP) pthread_cond_wait(&q->not_full, &q->mtx); q->buf[q->rear] = val; q->rear = (q->rear + 1) % QUEUE_CAP; q->count++; pthread_cond_signal(&q->not_empty); pthread_mutex_unlock(&q->mtx); }
static int queue_pop(TaskQueue *q, int *val) { pthread_mutex_lock(&q->mtx); while(q->count == 0 && !q->producer_done) pthread_cond_wait(&q->not_empty, &q->mtx); if(q->count == 0 && q->producer_done) { pthread_mutex_unlock(&q->mtx); return 0; } *val = q->buf[q->front]; q->front = (q->front + 1) % QUEUE_CAP; q->count--; pthread_cond_signal(&q->not_full); pthread_mutex_unlock(&q->mtx); return 1; }
static void queue_finish(TaskQueue *q) { pthread_mutex_lock(&q->mtx); q->producer_done = 1; pthread_cond_broadcast(&q->not_empty); pthread_mutex_unlock(&q->mtx); }

static int *g_db;
static unsigned char *g_valid;
static int *g_query_hist;
static char **g_names;
static int g_n;
static const char *g_db_dir;

static TaskQueue g_queue_index;
static TaskQueue g_queue_search;
static pthread_barrier_t g_barrier;

typedef struct __attribute__((aligned(64))) {
    int id, images_indexed, images_searched;
    Hit top[TOP_K];
    int topn;
    double t_index, t_search;
} WorkerData;

static void *worker(void *arg) {
    WorkerData *w = arg; char path[512]; unsigned char tmp_pixels[FEATURE_SIZE];
    double t0 = now_sec(); int block_id;
    while (queue_pop(&g_queue_index, &block_id)) {
        int lo = block_id * BLOCK_SIZE, hi = lo + BLOCK_SIZE;
        if (hi > g_n) hi = g_n;
        for (int i = lo; i < hi; i++) {
            snprintf(path, sizeof(path), "%s/%s", g_db_dir, g_names[i]);
            if (load_gray32(path, tmp_pixels)) {
                compute_histogram(tmp_pixels, g_db + i * HIST_BINS);
                g_valid[i] = 1;
            }
            w->images_indexed++;
        }
    }
    w->t_index = now_sec() - t0;
    pthread_barrier_wait(&g_barrier);

    t0 = now_sec();
    while (queue_pop(&g_queue_search, &block_id)) {
        int lo = block_id * BLOCK_SIZE, hi = lo + BLOCK_SIZE;
        if (hi > g_n) hi = g_n;
        for (int i = lo; i < hi; i++) {
            if (!g_valid[i]) continue;
            Hit h = { hist_distance(g_query_hist, g_db + i * HIST_BINS), i };
            topk_insert(w->top, &w->topn, h);
            w->images_searched++;
        }
    }
    w->t_search = now_sec() - t0;
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    int num_threads = (argc >= 3) ? atoi(argv[2]) : 4;
    g_db_dir = getenv("IMG_DB_DIR") ? getenv("IMG_DB_DIR") : "dataset/train";

    unsigned char query_pixels[FEATURE_SIZE];
    if (!load_gray32(argv[1], query_pixels)) return 1;
    int query_hist[HIST_BINS]; compute_histogram(query_pixels, query_hist);
    g_query_hist = query_hist;

    struct dirent **entries;
    int n = scan_png_dir(g_db_dir, &entries);
    g_n = n; int num_blocks = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;

    g_db = calloc(n, HIST_BINS * sizeof(int));
    g_valid = calloc(n, 1);
    g_names = malloc(n * sizeof(char *));
    for (int i = 0; i < n; i++) g_names[i] = entries[i]->d_name;

    printf("=== BARRIER POOL HISTOGRAM RETRIEVAL ===\nThreads: %d\n", num_threads);

    pthread_t *tids = malloc(num_threads * sizeof(pthread_t));
    WorkerData *wdata = aligned_alloc(64, num_threads * sizeof(WorkerData));
    memset(wdata, 0, num_threads * sizeof(WorkerData));

    queue_init(&g_queue_index); queue_init(&g_queue_search);
    pthread_barrier_init(&g_barrier, NULL, num_threads + 1);

    double t_total_start = now_sec();
    for (int i = 0; i < num_threads; i++) { wdata[i].id = i; pthread_create(&tids[i], NULL, worker, &wdata[i]); }

    double t_index_start = now_sec();
    for (int b = 0; b < num_blocks; b++) queue_push(&g_queue_index, b);
    queue_finish(&g_queue_index);
    for (int b = 0; b < num_blocks; b++) queue_push(&g_queue_search, b);
    queue_finish(&g_queue_search);

    pthread_barrier_wait(&g_barrier);
    double t_index_end = now_sec();
    double t_search_start = t_index_end;

    for (int i = 0; i < num_threads; i++) pthread_join(tids[i], NULL);
    double t_search_end = now_sec();

    Hit final_top[TOP_K]; int final_n = 0;
    for (int i = 0; i < num_threads; i++) topk_merge(final_top, &final_n, wdata[i].top, wdata[i].topn);
    double t_total_end = now_sec();
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);

    printf("\nTop %d Similar Images (Euclidean Histogram Distance):\n", TOP_K);
    for (int i = 0; i < final_n; i++) {
        printf("%d. %s/%s | Euclidean Dist: %d\n", i + 1, g_db_dir, g_names[final_top[i].idx], final_top[i].dist);
    }
    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);
    printf("Context switches: %ld voluntary, %ld involuntary\n", ru.ru_nvcsw, ru.ru_nivcsw);

    pthread_barrier_destroy(&g_barrier); queue_destroy(&g_queue_index); queue_destroy(&g_queue_search);
    for (int i = 0; i < n; i++) free(entries[i]); free(entries); free(g_db); free(g_valid); free(g_names); free(tids); free(wdata);
    return 0;
}
