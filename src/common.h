#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <png.h>
#include <math.h>

/* =========================================================
   CONSTANTS
   ========================================================= */

#define HIST_BINS     256         /* 256-bin grayscale histogram */
#define TOP_K         5
#define BLOCK_SIZE    64          /* images per pool task    */
#define FEATURE_SIZE  (32 * 32)   /* legacy size for reading pixels */

/* =========================================================
   HIT: one candidate result (Euclidean distance)
   ========================================================= */

typedef struct {
    int dist;   /* Euclidean Distance (0 is perfect match)    */
    int idx;    /* global image index for tie-breaking        */
} Hit;

/* =========================================================
   PUBLIC API
   ========================================================= */

int load_gray32(const char *path, unsigned char *pixels);

/* Compute a 256-bin histogram from 1024 grayscale pixels. */
void compute_histogram(const unsigned char *pixels, int *hist);

/* Compute Euclidean distance (squared L2 norm) between two 256-bin histograms. */
int hist_distance(const int *h1, const int *h2);

void topk_insert(Hit *top, int *n, Hit h);
void topk_merge(Hit *dst, int *dn, const Hit *src, int sn);

double now_sec(void);
int scan_png_dir(const char *dirpath, struct dirent ***out);

#endif /* COMMON_H */
