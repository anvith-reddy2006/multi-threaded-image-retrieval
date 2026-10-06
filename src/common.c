#include "common.h"

double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int load_gray32(const char *path, unsigned char *pixels)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) { fclose(fp); return 0; }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return 0; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return 0; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return 0; }
    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);
    png_uint_32 w, h;
    int depth, ctype;
    png_get_IHDR(png, info, &w, &h, &depth, &ctype, NULL, NULL, NULL);
    if (w != 32 || h != 32) longjmp(png_jmpbuf(png), 1);
    if (depth == 16) png_set_strip_16(png);
    if (ctype == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ctype == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    png_set_strip_alpha(png);
    if (ctype & PNG_COLOR_MASK_COLOR) png_set_rgb_to_gray_fixed(png, 1, -1, -1);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != 32) longjmp(png_jmpbuf(png), 1);
    png_bytep rows[32];
    for (int y = 0; y < 32; y++) rows[y] = pixels + y * 32;
    png_read_image(png, rows);
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return 1;
}

/* Calculate a 256-bin histogram from the 1024 pixel array */
void compute_histogram(const unsigned char *pixels, int *hist)
{
    memset(hist, 0, HIST_BINS * sizeof(int));
    for (int i = 0; i < FEATURE_SIZE; i++) {
        hist[pixels[i]]++;
    }
}

/* Euclidean Distance (L2 norm squared) between two 256-bin histograms */
int hist_distance(const int *h1, const int *h2)
{
    int sum = 0;
    for (int i = 0; i < HIST_BINS; i++) {
        int diff = h1[i] - h2[i];
        sum += (diff * diff);
    }
    return sum;
}

static inline int better(const Hit *a, const Hit *b)
{
    return a->dist < b->dist || (a->dist == b->dist && a->idx < b->idx);
}

void topk_insert(Hit *top, int *n, Hit h)
{
    for (int j = 0; j < *n; j++) {
        if (top[j].idx == h.idx) return;
    }
    if (*n == TOP_K && !better(&h, &top[TOP_K - 1])) return;
    int i = (*n < TOP_K) ? (*n)++ : TOP_K - 1;
    while (i > 0 && better(&h, &top[i - 1])) { top[i] = top[i - 1]; i--; }
    top[i] = h;
}

void topk_merge(Hit *dst, int *dn, const Hit *src, int sn)
{
    for (int i = 0; i < sn; i++) topk_insert(dst, dn, src[i]);
}

static int png_filter(const struct dirent *e)
{
    const char *name = e->d_name;
    size_t len = strlen(name);
    if (len <= 4) return 0;
    return strcmp(name + len - 4, ".png") == 0;
}

int scan_png_dir(const char *dirpath, struct dirent ***out)
{
    int n = scandir(dirpath, out, png_filter, alphasort);
    return (n < 0) ? 0 : n;
}
