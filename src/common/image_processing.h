#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#define HISTOGRAM_BINS 256

/* Returns the number of database images to use: the IMAGE_LIMIT environment
   variable if set (1..default_max), otherwise default_max. Used for the
   data-size (crossover) experiments. */
int get_image_limit(int default_max);

void calculate_histogram(const unsigned char *image,
                         int size,
                         double histogram[HISTOGRAM_BINS]);

double histogram_intersection(
    const double h1[HISTOGRAM_BINS],
    const double h2[HISTOGRAM_BINS]);

void histogram_equalization(
    const unsigned char *input,
    unsigned char *output,
    int size);

void gaussian_smoothing(
    const unsigned char *input,
    unsigned char *output,
    int width,
    int height);

#endif
