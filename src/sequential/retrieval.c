#include "../common.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <query.png>\n", argv[0]);
        return 1;
    }

    unsigned char query_pixels[FEATURE_SIZE];
    if (!load_gray32(argv[1], query_pixels)) {
        fprintf(stderr, "Cannot load query image: %s\n", argv[1]);
        return 1;
    }
    
    int query_hist[HIST_BINS];
    compute_histogram(query_pixels, query_hist);

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

    int search_repeat = 1;
    const char *sr_env = getenv("SEARCH_REPEAT");
    if (sr_env) {
        int sr = atoi(sr_env);
        if (sr > 1) search_repeat = sr;
    }

    /* db now stores 256-bin histograms instead of raw pixels */
    int           *db    = calloc(n, HIST_BINS * sizeof(int));
    unsigned char *valid = calloc(n, 1);
    char         **names = malloc(n * sizeof(char *));
    if (!db || !valid || !names) return 1;
    for (int i = 0; i < n; i++) names[i] = entries[i]->d_name;

    printf("=== SEQUENTIAL HISTOGRAM RETRIEVAL ===\n");
    printf("Images found: %d\n", n);

    double t_total_start = now_sec();

    /* Phase 1: Indexing (Decode PNG + Compute Histogram) */
    double t_index_start = now_sec();
    int loaded = 0, failed = 0;
    char path[512];
    unsigned char tmp_pixels[FEATURE_SIZE];
    
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "dataset/train/%s", names[i]);
        if (load_gray32(path, tmp_pixels)) {
            compute_histogram(tmp_pixels, db + i * HIST_BINS);
            valid[i] = 1;
            loaded++;
        } else { failed++; }
    }
    double t_index_end = now_sec();

    /* Phase 2: Searching (Euclidean Distance) */
    double t_search_start = now_sec();
    Hit top[TOP_K];
    int topn = 0;
    for (int rep = 0; rep < search_repeat; rep++) {
        for (int i = 0; i < n; i++) {
            if (!valid[i]) continue;
            Hit h = { hist_distance(query_hist, db + i * HIST_BINS), i };
            topk_insert(top, &topn, h);
        }
    }
    double t_search_end = now_sec();
    double t_total_end = now_sec();

    printf("Loaded: %d  Failed: %d\n", loaded, failed);
    printf("\nTop %d Similar Images (Euclidean Histogram Distance):\n", TOP_K);
    for (int i = 0; i < topn; i++) {
        printf("%d. dataset/train/%s | Euclidean Dist: %d\n", i + 1, names[top[i].idx], top[i].dist);
    }

    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);

    for (int i = 0; i < total_entries; i++) {
        free(entries[i]);
    }
    free(entries);
    free(db);
    free(valid);
    free(names);
    return 0;
}
