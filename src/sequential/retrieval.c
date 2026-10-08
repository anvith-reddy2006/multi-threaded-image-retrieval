#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <math.h>
#include <png.h>
#include "../common/image_processing.h"
#define MAX_IMAGES 10000
#define MAX_PATH 512
#define FEATURE_SIZE (32 * 32)
#define TOP_K 5

typedef struct {
    char path[MAX_PATH];
    unsigned char *data;
} ImageData;

typedef struct {
    char path[MAX_PATH];
    double distance;
} ImageResult;

/* Load PNG image and convert it to 32x32 grayscale. */
int load_grayscale(const char *filename,
                   unsigned char *pixels) {
    FILE *fp = fopen(filename, "rb");

    if (!fp) {
        perror(filename);
        return 0;
    }

    unsigned char header[8];

    if (fread(header, 1, 8, fp) != 8 ||
        png_sig_cmp(header, 0, 8)) {
        fclose(fp);
        return 0;
    }

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING,
                               NULL, NULL, NULL);

    if (!png) {
        fclose(fp);
        return 0;
    }

    png_infop info =
        png_create_info_struct(png);

    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return 0;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 width, height;
    int bit_depth, color_type;

    png_get_IHDR(png, info,
                 &width, &height,
                 &bit_depth, &color_type,
                 NULL, NULL, NULL);

    if (width != 32 || height != 32) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    if (bit_depth == 16)
        png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_rgb_to_gray_fixed(png, 1, -1, -1);
    }

    if (color_type & PNG_COLOR_MASK_ALPHA)
        png_set_strip_alpha(png);

    png_read_update_info(png, info);

    png_bytep row =
        (png_bytep)malloc(png_get_rowbytes(png, info));

    if (!row) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    for (int y = 0; y < 32; y++) {
        png_read_row(png, row, NULL);
        memcpy(&pixels[y * 32], row, 32);
    }

    free(row);

    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return 1;
}

/* Calculate histogram similarity between two images. */
double calculate_similarity(const unsigned char *a,
                            const unsigned char *b) {
    double histogram_a[HISTOGRAM_BINS];
    double histogram_b[HISTOGRAM_BINS];

    calculate_histogram(
        a,
        FEATURE_SIZE,
        histogram_a
    );

    calculate_histogram(
        b,
        FEATURE_SIZE,
        histogram_b
    );

    return histogram_intersection(
        histogram_a,
        histogram_b
    );
}
/* Sort results by descending similarity. */
int compare_results(const void *a,
                    const void *b) {
    const ImageResult *x = a;
    const ImageResult *y = b;

    if (x->distance > y->distance)
        return -1;

    if (x->distance < y->distance)
        return 1;

    /* equal similarity: order by file name so every version gives the same top-5 */
    return strcmp(x->path, y->path);
}
int main(int argc, char *argv[]) {

    if (argc != 3) {
    printf("Usage: %s <query_image.png> <scenario>\n",
           argv[0]);
    printf("Scenario 1 = Original\n");
    printf("Scenario 2 = Histogram Equalization\n");
    printf("Scenario 3 = Gaussian Smoothing\n");
    printf("Scenario 4 = Same Domain, Different Image\n");
    return 1;
}

int scenario = atoi(argv[2]);

if (scenario < 1 || scenario > 4) {
    printf("Invalid scenario. Use 1, 2, 3, or 4.\n");
    return 1;
}

    /* Load query image. */
    /* Load query image. */
unsigned char query[FEATURE_SIZE];
unsigned char processed_query[FEATURE_SIZE];

if (!load_grayscale(argv[1], query)) {
    fprintf(stderr,
            "Could not load query image.\n");
    return 1;
}

if (scenario == 1) {
    memcpy(processed_query, query, FEATURE_SIZE);
}
else if (scenario == 2) {
    histogram_equalization(
        query,
        processed_query,
        FEATURE_SIZE
    );
}
else if (scenario == 3) {
    gaussian_smoothing(
        query,
        processed_query,
        32,
        32
    );
}
else if (scenario == 4) {
    memcpy(processed_query, query, FEATURE_SIZE);
}

    DIR *dir = opendir("dataset/train");

    if (!dir) {
        perror("dataset/train");
        return 1;
    }

    /* Allocate memory for all images. */
    ImageData *images =
        malloc(MAX_IMAGES * sizeof(ImageData));

    if (!images) {
        perror("malloc");
        closedir(dir);
        return 1;
    }

    int image_count = 0;
    const int image_cap = get_image_limit(MAX_IMAGES);

    struct dirent *entry;

    printf("=== SEQUENTIAL IMAGE RETRIEVAL ===\n");
    printf("Loading images into memory...\n");

    /* Load all images BEFORE timing. */
    while ((entry = readdir(dir)) != NULL &&
           image_count < image_cap) {

        if (strstr(entry->d_name, ".png") == NULL)
            continue;

        snprintf(images[image_count].path,
                 MAX_PATH,
                 "dataset/train/%s",
                 entry->d_name);

        images[image_count].data =
            malloc(FEATURE_SIZE);

        if (!images[image_count].data) {
            perror("malloc");
            closedir(dir);

            for (int i = 0; i < image_count; i++)
                free(images[i].data);

            free(images);
            return 1;
        }

        if (!load_grayscale(
                images[image_count].path,
                images[image_count].data)) {

            free(images[image_count].data);
            continue;
        }

        image_count++;
    }

    closedir(dir);

    printf("All images loaded.\n");
    printf("Images: %d\n", image_count);

    /* Allocate result array. */
    ImageResult *results =
        malloc(image_count * sizeof(ImageResult));

    if (!results) {
        perror("malloc");

        for (int i = 0; i < image_count; i++)
            free(images[i].data);

        free(images);
        return 1;
    }

    /*
     * Start timing ONLY the sequential
     * similarity computation.
     */
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < image_count; i++) {

        snprintf(results[i].path,
                 MAX_PATH,
                 "%s",
                 images[i].path);

        results[i].distance =
    calculate_similarity(
        processed_query,
        images[i].data);
    }

        clock_gettime(CLOCK_MONOTONIC, &end);

    qsort(results,
          image_count,
          sizeof(ImageResult),
          compare_results);

    double elapsed =
        (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) / 1e9;

    /* Display results. */
    printf("\nTop %d Similar Images:\n", TOP_K);

    int limit =
        image_count < TOP_K
        ? image_count
        : TOP_K;

    for (int i = 0; i < limit; i++) {

        printf("%d. %s | Similarity: %.4f\n",
       i + 1,
       results[i].path,
       results[i].distance);
    }

    printf("\nExecution Time: %.6f seconds\n",
           elapsed);

    /* Free memory. */
    for (int i = 0; i < image_count; i++)
        free(images[i].data);

    free(results);
    free(images);

    return 0;
}
