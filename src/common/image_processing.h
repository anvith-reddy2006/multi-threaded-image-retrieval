#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#define HISTOGRAM_BINS 256

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
