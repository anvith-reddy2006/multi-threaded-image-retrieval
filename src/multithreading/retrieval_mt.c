#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <png.h>
#include <math.h>
#include <time.h>
#include "../common/image_processing.h"
#define MAX_IMAGES 10000
#define FEATURE_SIZE (32 * 32)
#define MAX_FILENAME 256
#define MAX_PATH 512
#define TOP_K 5

typedef struct {
    char filename[MAX_FILENAME];
    unsigned char *data;
} ImageData;

typedef struct {
    char filename[MAX_FILENAME];
    double distance;
} ImageResult;

typedef struct {
    ImageData *images;
    unsigned char *query;
    ImageResult *results;
    int start;
    int end;
} ThreadData;

/* Load PNG image and convert it to 32x32 grayscale. */
int load_grayscale(const char *filename,
                   unsigned char *pixels)
{
    FILE *fp = fopen(filename, "rb");

    if (!fp)
        return 0;

    unsigned char header[8];

    if (fread(header, 1, 8, fp) != 8 ||
        png_sig_cmp(header, 0, 8)) {
        fclose(fp);
        return 0;
    }

    png_structp png =
        png_create_read_struct(
            PNG_LIBPNG_VER_STRING,
            NULL,
            NULL,
            NULL
        );

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

    png_get_IHDR(
        png,
        info,
        &width,
        &height,
        &bit_depth,
        &color_type,
        NULL,
        NULL,
        NULL
    );

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

        png_set_rgb_to_gray_fixed(
            png,
            1,
            -1,
            -1
        );
    }

    if (color_type & PNG_COLOR_MASK_ALPHA)
        png_set_strip_alpha(png);

    png_read_update_info(png, info);

    png_bytep row =
        (png_bytep)malloc(
            png_get_rowbytes(png, info)
        );

    if (!row) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    for (int y = 0; y < 32; y++) {

        png_read_row(png, row, NULL);

        memcpy(
            &pixels[y * 32],
            row,
            32
        );
    }

    free(row);

    png_read_end(png, NULL);

    png_destroy_read_struct(
        &png,
        &info,
        NULL
    );

    fclose(fp);

    return 1;
}

/* Calculate histogram similarity. */
double calculate_similarity(
    const unsigned char *a,
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

/* Worker thread. */
void *worker(void *arg)
{
    ThreadData *data =
        (ThreadData *)arg;

    for (int i = data->start;
         i < data->end;
         i++) {

        double similarity =
    calculate_similarity(
        data->query,
        data->images[i].data
    );

strcpy(
    data->results[
        i - data->start
    ].filename,
    data->images[i].filename
);

data->results[
    i - data->start
].distance = similarity;
    }

    return NULL;
}

/* Sort by descending similarity. */
int compare_results(const void *a,
                    const void *b) {
    const ImageResult *x = a;
    const ImageResult *y = b;

    if (x->distance > y->distance)
        return -1;

    if (x->distance < y->distance)
        return 1;

    return 0;
}

int main(int argc, char *argv[])
{
       if (argc != 4) {

    printf(
        "Usage: %s <query_image.png> <scenario> <num_threads>\n",
        argv[0]
    );

    printf("Scenario 1 = Original\n");
    printf("Scenario 2 = Histogram Equalization\n");
    printf("Scenario 3 = Gaussian Smoothing\n");
    printf("Scenario 4 = Same Domain, Different Image\n");

    return 1;
}

        
    int scenario =
    atoi(argv[2]);

    int num_threads =
    atoi(argv[3]);
    if (scenario < 1 ||
    scenario > 4) {

    printf(
        "Invalid scenario. Use 1, 2, 3, or 4.\n"
    );

    return 1;
}
    if (num_threads < 1 ||
        num_threads > 32) {

        printf(
            "Number of threads must be between 1 and 32.\n"
        );

        return 1;
    }

    /* Load query image. */
unsigned char query[FEATURE_SIZE];
unsigned char processed_query[FEATURE_SIZE];

if (!load_grayscale(
        argv[1],
        query)) {

    printf(
        "Error loading query image: %s\n",
        argv[1]
    );

    return 1;
}

/* Apply selected scenario. */
if (scenario == 1) {

    memcpy(
        processed_query,
        query,
        FEATURE_SIZE
    );

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

    memcpy(
        processed_query,
        query,
        FEATURE_SIZE
    );
}

    /* Open dataset. */
    DIR *dir =
        opendir("dataset/train");

    if (!dir) {

        perror("dataset/train");

        return 1;
    }

    ImageData *images =
        malloc(
            MAX_IMAGES *
            sizeof(ImageData)
        );

    if (!images) {

        perror("malloc");

        closedir(dir);

        return 1;
    }

    struct dirent *entry;

    int image_count = 0;
    int image_limit = get_image_limit(MAX_IMAGES);
    printf(
        "=== STATIC MULTITHREADED IMAGE RETRIEVAL ===\n"
    );

    /* Find images. */
    while ((entry = readdir(dir)) != NULL) {

        if (image_count >= image_limit)
            break;

        if (strstr(
                entry->d_name,
                ".png") == NULL)
            continue;

        strcpy(
            images[image_count].filename,
            entry->d_name
        );

        images[image_count].data =
            malloc(FEATURE_SIZE);

        if (!images[image_count].data) {

            perror("malloc");

            closedir(dir);

            return 1;
        }

        image_count++;
    }

    closedir(dir);

    printf(
        "Images: %d\n",
        image_count
    );

    printf(
        "Threads: %d\n",
        num_threads
    );

    printf(
        "Loading images into memory...\n"
    );

    /* Load all images BEFORE timing. */
    for (int i = 0;
         i < image_count;
         i++) {

        char path[MAX_PATH];

        snprintf(
            path,
            sizeof(path),
            "dataset/train/%s",
            images[i].filename
        );

        if (!load_grayscale(
                path,
                images[i].data)) {

            printf(
                "Error loading: %s\n",
                path
            );

            for (int j = 0;
                 j < image_count;
                 j++)
                free(images[j].data);

            free(images);

            return 1;
        }
    }

    printf(
        "All images loaded.\n"
    );

    /* Allocate thread structures. */
    pthread_t *threads =
        malloc(
            num_threads *
            sizeof(pthread_t)
        );

    ThreadData *thread_data =
        malloc(
            num_threads *
            sizeof(ThreadData)
        );

    ImageResult **thread_results =
        malloc(
            num_threads *
            sizeof(ImageResult *)
        );

    if (!threads ||
        !thread_data ||
        !thread_results) {

        perror("malloc");

        return 1;
    }

    /*
     * Each thread gets its own result array.
     * This avoids races on shared result data.
     */
    for (int i = 0;
         i < num_threads;
         i++) {

        thread_results[i] =
            malloc(
                MAX_IMAGES *
                sizeof(ImageResult)
            );

        if (!thread_results[i]) {

            perror("malloc");

            return 1;
        }
    }

    struct timespec start_time;
    struct timespec end_time;

    /*
     * Start timing only the parallel
     * similarity computation and
     * thread creation/join overhead.
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &start_time
    );

    int images_per_thread =
        image_count / num_threads;

    int remaining =
        image_count % num_threads;

    int current_start = 0;

    /* Create worker threads. */
    for (int i = 0;
         i < num_threads;
         i++) {

        int current_end =
            current_start +
            images_per_thread;

        if (i == num_threads - 1)
            current_end += remaining;

        thread_data[i].images =
            images;

        thread_data[i].query =
            processed_query;

        thread_data[i].results =
            thread_results[i];

        thread_data[i].start =
            current_start;

        thread_data[i].end =
            current_end;

        if (pthread_create(
                &threads[i],
                NULL,
                worker,
                &thread_data[i]
            ) != 0) {

            printf(
                "Error creating thread %d\n",
                i
            );

            return 1;
        }

        current_start =
            current_end;
    }

    /* Wait for all workers. */
    for (int i = 0;
         i < num_threads;
         i++) {

        pthread_join(
            threads[i],
            NULL
        );
    }

    /* Stop timer BEFORE sorting. */
    clock_gettime(
        CLOCK_MONOTONIC,
        &end_time
    );

    double elapsed =
        (end_time.tv_sec -
         start_time.tv_sec)
        +
        (end_time.tv_nsec -
         start_time.tv_nsec)
        / 1000000000.0;

    /* Merge thread results. */
    ImageResult *all_results =
        malloc(
            image_count *
            sizeof(ImageResult)
        );

    if (!all_results) {

        perror("malloc");

        return 1;
    }

    int result_index = 0;

    for (int i = 0;
         i < num_threads;
         i++) {

        int count =
            thread_data[i].end -
            thread_data[i].start;

        for (int j = 0;
             j < count;
             j++) {

            all_results[result_index] =
                thread_results[i][j];

            result_index++;
        }
    }

    /* Sort AFTER timing. */
    qsort(
        all_results,
        image_count,
        sizeof(ImageResult),
        compare_results
    );

    /* Display results. */
    printf(
        "\nTop %d Similar Images:\n",
        TOP_K
    );

    int limit =
        image_count < TOP_K
        ? image_count
        : TOP_K;

    for (int i = 0;
         i < limit;
         i++) {

        printf(
            "%d. dataset/train/%s | Similarity: %.4f\n",
            i + 1,
            all_results[i].filename,
            all_results[i].distance
        );
    }

    printf(
        "\nExecution Time: %.6f seconds\n",
        elapsed
    );

    /* Free memory. */
    free(all_results);

    for (int i = 0;
         i < num_threads;
         i++)
        free(thread_results[i]);

    free(thread_results);
    free(threads);
    free(thread_data);

    for (int i = 0;
         i < image_count;
         i++)
        free(images[i].data);

    free(images);

    return 0;
}
