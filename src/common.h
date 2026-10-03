#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <png.h>

/* =========================================================
   CONSTANTS
   ========================================================= */

#define FEATURE_SIZE  (32 * 32)   /* 1024 bytes per image    */
#define TOP_K         5
#define BLOCK_SIZE    64          /* images per pool task    */

/* =========================================================
   HIT: one candidate result (distance + global index)
   ========================================================= */

typedef struct {
    int dist;   /* squared Euclidean distance (max 66 586 624) */
    int idx;    /* global image index for tie-breaking         */
} Hit;

/* =========================================================
   PUBLIC API  (implemented in common.c)
   ========================================================= */

/*
 * Load a 32x32 PNG of any supported colour type into 1024
 * grayscale bytes.  Returns 1 on success, 0 on any error
 * (wrong size, corrupt, missing, unsupported).  Never crashes.
 */
int load_gray32(const char *path, unsigned char *pixels);

/*
 * Squared Euclidean distance between two 1024-byte vectors.
 * Result fits in a 32-bit int.
 */
int sq_distance(const unsigned char *a, const unsigned char *b);

/*
 * Insert a candidate into a sorted (best-first) top-k array.
 * Tie-break: lower index wins.
 */
void topk_insert(Hit *top, int *n, Hit h);

/*
 * Merge src[0..sn-1] into dst[0..*dn-1].  Both arrays are
 * sorted best-first.  *dn is updated.
 */
void topk_merge(Hit *dst, int *dn, const Hit *src, int sn);

/*
 * CLOCK_MONOTONIC seconds (high-resolution).
 */
double now_sec(void);

/*
 * Scan a directory for .png files.  Returns a sorted list
 * of basenames via scandir + alphasort.  Caller must free
 * each entry and the array.
 */
int scan_png_dir(const char *dirpath, struct dirent ***out);

#endif /* COMMON_H */
