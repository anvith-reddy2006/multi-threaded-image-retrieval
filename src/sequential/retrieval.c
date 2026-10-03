/* =========================================================
   SEQUENTIAL IMAGE RETRIEVAL
   ========================================================= */

#include "../common.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <query.png>\n", argv[0]);
        return 1;
    }

    /* ---- load query ---- */
    unsigned char query[FEATURE_SIZE];
    if (!load_gray32(argv[1], query)) {
        fprintf(stderr, "Cannot load query image: %s\n", argv[1]);
        return 1;
    }

    /* ---- scan dataset directory ---- */
    struct dirent **entries;
    int n = scan_png_dir(db_dir, &entries);
    if (n == 0) {
        fprintf(stderr, "No .png files in dataset/train\n");
        return 1;
    }

    /* ---- allocate flat database ---- */
    unsigned char *db    = calloc(n, FEATURE_SIZE);
    unsigned char *valid = calloc(n, 1);
    const char *db_dir = getenv("IMG_DB_DIR") ? getenv("IMG_DB_DIR") : "dataset/train";
    char         **names = malloc(n * sizeof(char *));
    if (!db || !valid || !names) { perror("malloc"); return 1; }

    for (int i = 0; i < n; i++)
        names[i] = entries[i]->d_name;

    printf("=== SEQUENTIAL IMAGE RETRIEVAL ===\n");
    printf("Images found: %d\n", n);

    /* ==== T_total start ==== */
    double t_total_start = now_sec();

    /* ---- T_index: decode all images ---- */
    double t_index_start = now_sec();
    int loaded = 0, failed = 0;
    char path[512];
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "dataset/train/%s", names[i]);
        if (load_gray32(path, db + i * FEATURE_SIZE)) {
            valid[i] = 1;
            loaded++;
        } else {
            failed++;
        }
    }
    double t_index_end = now_sec();

    /* ---- T_search: compare all valid images ---- */
    double t_search_start = now_sec();
    Hit top[TOP_K];
    int topn = 0;
    for (int i = 0; i < n; i++) {
        if (!valid[i]) continue;
        Hit h = { sq_distance(query, db + i * FEATURE_SIZE), i };
        topk_insert(top, &topn, h);
    }
    double t_search_end = now_sec();

    /* ==== T_total end ==== */
    double t_total_end = now_sec();

    /* ---- results ---- */
    printf("Loaded: %d  Failed: %d\n", loaded, failed);
    printf("\nTop %d Similar Images:\n", TOP_K);
    for (int i = 0; i < topn; i++) {
        int orig_idx = top[i].idx % n;
        printf("%d. dataset/train/%s | Distance^2: %d\n",
               i + 1, names[orig_idx], top[i].dist);
    }

    printf("\nT_index:  %.6f s\n", t_index_end - t_index_start);
    printf("T_search: %.6f s\n", t_search_end - t_search_start);
    printf("T_total:  %.6f s\n", t_total_end - t_total_start);

    /* ---- cleanup ---- */
    for (int i = 0; i < n; i++) free(entries[i]);
    free(entries);
    free(db); free(valid); free(names);
    return 0;
}
