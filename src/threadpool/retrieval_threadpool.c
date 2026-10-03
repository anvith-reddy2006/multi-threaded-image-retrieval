#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <png.h>
#include <math.h>
#include <time.h>

#define MAX_IMAGES 10000
#define MAX_PATH 512
#define FEATURE_SIZE (32 * 32)
#define TOP_K 5

#define QUEUE_SIZE 32

typedef struct {
    char path[MAX_PATH];
    unsigned char pixels[FEATURE_SIZE];
} ImageData;

typedef struct {
    char path[MAX_PATH];
    double distance;
} ImageResult;

/* =========================
   TASK QUEUE
   ========================= */

typedef struct {
    int tasks[QUEUE_SIZE];

    int front;
    int rear;
    int count;

    int producer_done;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;

} TaskQueue;


/* =========================
   WORKER DATA
   ========================= */

typedef struct {

    int thread_id;

    ImageData *images;
    int image_count;

    unsigned char *query;

    TaskQueue *queue;

    ImageResult *results;
    int result_count;

} WorkerData;


/* =========================
   HIGH RESOLUTION TIMER
   ========================= */

double get_time_seconds()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}


/* =========================
   PNG LOADER
   ========================= */

int load_grayscale(const char *filename,
                   unsigned char *output)
{
    FILE *fp = fopen(filename, "rb");

    if (!fp)
        return 0;

    unsigned char header[8];

    if (fread(header, 1, 8, fp) != 8)
    {
        fclose(fp);
        return 0;
    }

    if (png_sig_cmp(header, 0, 8))
    {
        fclose(fp);
        return 0;
    }

    png_structp png =
        png_create_read_struct(
            PNG_LIBPNG_VER_STRING,
            NULL,
            NULL,
            NULL);

    if (!png)
    {
        fclose(fp);
        return 0;
    }

    png_infop info =
        png_create_info_struct(png);

    if (!info)
    {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return 0;
    }

    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);

    png_set_sig_bytes(png, 8);

    png_read_info(png, info);

    int width =
        png_get_image_width(png, info);

    int height =
        png_get_image_height(png, info);

    int color_type =
        png_get_color_type(png, info);

    int bit_depth =
        png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_RGBA ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_strip_alpha(png);

    if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_RGB ||
    color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
    color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_rgb_to_gray_fixed(png, 1, -1, -1);

if (color_type & PNG_COLOR_MASK_ALPHA)
    png_set_strip_alpha(png);

    png_read_update_info(png, info);

    png_bytep rows[32];

    for (int y = 0; y < height && y < 32; y++)
        rows[y] = malloc(png_get_rowbytes(png, info));

    png_read_image(png, rows);

    for (int y = 0; y < height && y < 32; y++)
    {
        for (int x = 0; x < width && x < 32; x++)
        {
            output[y * 32 + x] =
                rows[y][x];
        }
    }

    for (int y = 0; y < height && y < 32; y++)
        free(rows[y]);

    png_destroy_read_struct(
        &png,
        &info,
        NULL);

    fclose(fp);

    return 1;
}


/* =========================
   EUCLIDEAN DISTANCE
   ========================= */

double calculate_distance(
    unsigned char *a,
    unsigned char *b)
{
    double sum = 0.0;

    for (int i = 0; i < FEATURE_SIZE; i++)
    {
        double diff =
            (double)a[i] - (double)b[i];

        sum += diff * diff;
    }

    return sqrt(sum);
}


/* =========================
   TASK QUEUE INITIALIZATION
   ========================= */

void queue_init(TaskQueue *queue)
{
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    queue->producer_done = 0;

    pthread_mutex_init(
        &queue->mutex,
        NULL);

    pthread_cond_init(
        &queue->not_empty,
        NULL);

    pthread_cond_init(
        &queue->not_full,
        NULL);
}


/* =========================
   PRODUCER
   ========================= */

void *producer(void *arg)
{
    WorkerData *data =
        (WorkerData *)arg;

    TaskQueue *queue =
        data->queue;

    for (int i = 0;
         i < data->image_count;
         i++)
    {
        pthread_mutex_lock(
            &queue->mutex);

        /*
         * CRITICAL SECTION
         *
         * Only one thread can
         * modify the queue here.
         */

        while (queue->count == QUEUE_SIZE)
        {
            pthread_cond_wait(
                &queue->not_full,
                &queue->mutex);
        }

        queue->tasks[queue->rear] = i;

        queue->rear =
            (queue->rear + 1)
            % QUEUE_SIZE;

        queue->count++;

        /*
         * Wake a worker waiting
         * for a task.
         */

        pthread_cond_signal(
            &queue->not_empty);

        pthread_mutex_unlock(
            &queue->mutex);
    }

    /*
     * Tell workers that no more
     * tasks will be produced.
     */

    pthread_mutex_lock(
        &queue->mutex);

    queue->producer_done = 1;

    pthread_cond_broadcast(
        &queue->not_empty);

    pthread_mutex_unlock(
        &queue->mutex);

    return NULL;
}


/* =========================
   WORKER THREAD
   ========================= */

void *worker(void *arg)
{
    WorkerData *data =
        (WorkerData *)arg;

    printf(
        "Worker %d started\n",
        data->thread_id);

    while (1)
    {
        int task_index;

        pthread_mutex_lock(
            &data->queue->mutex);

        /*
         * Worker waits if queue
         * is empty and producer
         * is still working.
         */

        while (data->queue->count == 0 &&
               !data->queue->producer_done)
        {
            pthread_cond_wait(
                &data->queue->not_empty,
                &data->queue->mutex);
        }

        /*
         * No more work.
         */

        if (data->queue->count == 0 &&
            data->queue->producer_done)
        {
            pthread_mutex_unlock(
                &data->queue->mutex);

            break;
        }

        /*
         * CRITICAL SECTION
         *
         * Remove exactly one task.
         */

        task_index =
            data->queue->tasks[
                data->queue->front];

        data->queue->front =
            (data->queue->front + 1)
            % QUEUE_SIZE;

        data->queue->count--;

        /*
         * Tell producer that
         * queue space is available.
         */

        pthread_cond_signal(
            &data->queue->not_full);

        pthread_mutex_unlock(
            &data->queue->mutex);


        /*
         * Dynamic scheduling:
         *
         * Every worker takes the
         * next available task.
         */

        ImageResult *result =
            &data->results[
                data->result_count];

        snprintf(
            result->path,
            MAX_PATH,
            "%s",
            data->images[
                task_index].path);

        result->distance =
            calculate_distance(
                data->query,
                data->images[
                    task_index].pixels);

        data->result_count++;
    }

    printf(
        "Worker %d finished: %d images\n",
        data->thread_id,
        data->result_count);

    return NULL;
}


/* =========================
   RESULT SORTING
   ========================= */

int compare_results(
    const void *a,
    const void *b)
{
    ImageResult *r1 =
        (ImageResult *)a;

    ImageResult *r2 =
        (ImageResult *)b;

    if (r1->distance < r2->distance)
        return -1;

    if (r1->distance > r2->distance)
        return 1;

    return 0;
}


/* =========================
   MAIN
   ========================= */

int main(
    int argc,
    char *argv[])
{
    if (argc < 2)
    {
        printf(
            "Usage: %s <query_image> [threads]\n",
            argv[0]);

        return 1;
    }

    int num_threads = 4;

    if (argc >= 3)
    {
        num_threads =
            atoi(argv[2]);

        if (num_threads < 1)
            num_threads = 1;

        if (num_threads > 32)
            num_threads = 32;
    }

    char *query_path =
        argv[1];

    ImageData *images = malloc(MAX_IMAGES * sizeof(ImageData));

if (images == NULL)
{
    perror("malloc");
    return 1;
}

    int image_count = 0;

    /*
     * Load dataset filenames.
     */

    DIR *dir =
        opendir("dataset/train");

    if (!dir)
    {
        perror(
            "dataset/train");

        return 1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strstr(entry->d_name, ".png") == NULL)
            continue;

        if (image_count >= MAX_IMAGES)
            break;

        snprintf(
            images[image_count].path,
            MAX_PATH,
            "dataset/train/%s",
            entry->d_name);

        image_count++;
    }

    closedir(dir);

    printf(
        "Images found: %d\n",
        image_count);


    /*
     * Load query image.
     */

    unsigned char query[
        FEATURE_SIZE];

    if (!load_grayscale(
            query_path,
            query))
    {
        printf(
            "Failed to load query image.\n");

        return 1;
    }


    /*
     * PRELOAD DATASET
     *
     * This removes repeated
     * disk/PNG I/O from workers.
     */

    printf(
        "Loading images into memory...\n");

    for (int i = 0;
         i < image_count;
         i++)
    {
        if (!load_grayscale(
                images[i].path,
                images[i].pixels))
        {
            printf(
                "Failed to load %s\n",
                images[i].path);
        }
    }


    /*
     * Initialize task queue.
     */

    TaskQueue queue;

    queue_init(&queue);


    /*
     * Allocate worker structures.
     */

    pthread_t workers[32];

    WorkerData worker_data[32];

    ImageResult **worker_results =
    malloc(num_threads * sizeof(ImageResult *));

if (worker_results == NULL)
{
    perror("malloc");
    return 1;
}

for (int i = 0; i < num_threads; i++)
{
    worker_results[i] =
        malloc(MAX_IMAGES * sizeof(ImageResult));

    if (worker_results[i] == NULL)
    {
        perror("malloc");
        return 1;
    }
}


    /*
     * Timer starts AFTER
     * dataset loading.
     *
     * Therefore we measure
     * retrieval computation,
     * scheduling and
     * synchronization.
     */

    double start =
        get_time_seconds();


    /*
     * Create worker threads.
     */

    for (int i = 0;
         i < num_threads;
         i++)
    {
        worker_data[i].thread_id = i;

        worker_data[i].images =
            images;

        worker_data[i].image_count =
            image_count;

        worker_data[i].query =
            query;

        worker_data[i].queue =
            &queue;

        worker_data[i].results =
            worker_results[i];

        worker_data[i].result_count = 0;

        pthread_create(
            &workers[i],
            NULL,
            worker,
            &worker_data[i]);
    }


    /*
     * Producer thread.
     */

    pthread_t producer_thread;

    WorkerData producer_data;

    producer_data.images =
        images;

    producer_data.image_count =
        image_count;

    producer_data.queue =
        &queue;

    pthread_create(
        &producer_thread,
        NULL,
        producer,
        &producer_data);


    /*
     * Wait for producer.
     */

    pthread_join(
        producer_thread,
        NULL);


    /*
     * Wait for workers.
     */

    int total_results = 0;

    ImageResult *all_results =
    malloc(MAX_IMAGES * sizeof(ImageResult));

if (all_results == NULL)
{
    perror("malloc");
    return 1;
}
    for (int i = 0;
         i < num_threads;
         i++)
    {
        pthread_join(
            workers[i],
            NULL);

        for (int j = 0;
             j < worker_data[i].result_count;
             j++)
        {
            all_results[
                total_results++] =
                worker_results[i][j];
        }
    }


    double end =
        get_time_seconds();


    /*
     * Sort final results.
     */

    qsort(
        all_results,
        total_results,
        sizeof(ImageResult),
        compare_results);


    printf(
        "\n=== THREAD POOL IMAGE RETRIEVAL ===\n");

    printf(
        "Query image: %s\n",
        query_path);

    printf(
        "Images compared: %d\n",
        total_results);

    printf(
        "Worker threads: %d\n",
        num_threads);

    printf(
        "Scheduling: Dynamic\n");

    printf(
        "Synchronization: Mutex + Condition Variables\n");

    printf(
        "Task model: Producer-Consumer\n");

    printf(
        "\nTop %d similar images:\n",
        TOP_K);

    for (int i = 0;
         i < TOP_K &&
         i < total_results;
         i++)
    {
        printf(
            "%d. %s | Distance: %.2f\n",
            i + 1,
            all_results[i].path,
            all_results[i].distance);
    }

    printf(
        "\nExecution time: %.6f seconds\n",
        end - start);


    /*
     * Cleanup synchronization objects.
     */

    pthread_mutex_destroy(
        &queue.mutex);

    pthread_cond_destroy(
        &queue.not_empty);

    pthread_cond_destroy(
        &queue.not_full);
     for (int i = 0; i < num_threads; i++)
{
    free(worker_results[i]);
}

free(worker_results);
free(all_results); 
 free(images);
  return 0;
}
