#include "image_processing.h"
#include <stdlib.h>
void calculate_histogram(const unsigned char *image,
                         int size,
                         double histogram[HISTOGRAM_BINS])
{
    for (int i = 0; i < HISTOGRAM_BINS; i++)
        histogram[i] = 0.0;

    for (int i = 0; i < size; i++)
        histogram[image[i]]++;

    for (int i = 0; i < HISTOGRAM_BINS; i++)
        histogram[i] /= size;
}

double histogram_intersection(
    const double h1[HISTOGRAM_BINS],
    const double h2[HISTOGRAM_BINS])
{
    double similarity = 0.0;

    for (int i = 0; i < HISTOGRAM_BINS; i++) {
        similarity += h1[i] < h2[i] ? h1[i] : h2[i];
    }

    return similarity;
}
void histogram_equalization(
    const unsigned char *input,
    unsigned char *output,
    int size)
{
    int histogram[HISTOGRAM_BINS] = {0};
    int cdf[HISTOGRAM_BINS] = {0};

    /* Calculate histogram */
    for (int i = 0; i < size; i++)
        histogram[input[i]]++;

    /* Calculate cumulative distribution */
    cdf[0] = histogram[0];

    for (int i = 1; i < HISTOGRAM_BINS; i++)
        cdf[i] = cdf[i - 1] + histogram[i];

    /* Find first non-zero CDF value */
    int cdf_min = 0;

    while (cdf_min < HISTOGRAM_BINS &&
           cdf[cdf_min] == 0)
        cdf_min++;

    /* Apply histogram equalization */
    for (int i = 0; i < size; i++) {

        int pixel = input[i];

        if (cdf[255] == cdf_min) {
            output[i] = input[i];
        } else {
            output[i] =
                (unsigned char)(
                    ((cdf[pixel] - cdf_min) * 255) /
                    (cdf[255] - cdf_min)
                );
        }
    }
}
void gaussian_smoothing(
    const unsigned char *input,
    unsigned char *output,
    int width,
    int height)
{
    /* 5x5 Gaussian kernel, sigma approximately 1 */
    const int kernel[5][5] = {
        {1,  4,  6,  4, 1},
        {4, 16, 24, 16, 4},
        {6, 24, 36, 24, 6},
        {4, 16, 24, 16, 4},
        {1,  4,  6,  4, 1}
    };

    const int kernel_sum = 256;

    for (int y = 0; y < height; y++) {

        for (int x = 0; x < width; x++) {

            int sum = 0;

            for (int ky = -2; ky <= 2; ky++) {

                for (int kx = -2; kx <= 2; kx++) {

                    int nx = x + kx;
                    int ny = y + ky;

                    /* Reflect at image boundaries */
                    if (nx < 0)
                        nx = -nx;

                    if (nx >= width)
                        nx = 2 * width - nx - 2;

                    if (ny < 0)
                        ny = -ny;

                    if (ny >= height)
                        ny = 2 * height - ny - 2;

                    sum += input[ny * width + nx] *
                           kernel[ky + 2][kx + 2];
                }
            }

            sum /= kernel_sum;

            if (sum < 0)
                sum = 0;

            if (sum > 255)
                sum = 255;

            output[y * width + x] = (unsigned char)sum;
        }
     }
  }
int get_image_limit(int default_max)
{
    const char *env = getenv("IMAGE_LIMIT");

    if (env == NULL)
        return default_max;

    int limit = atoi(env);

    if (limit <= 0 || limit > default_max)
        return default_max;

    return limit;
}
