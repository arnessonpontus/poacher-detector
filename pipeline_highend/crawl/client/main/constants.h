#ifndef CONSTANTS_H
#define CONSTANTS_H

#define WIDTH 800
#define HEIGHT 600
#define NUM_CHANNELS 3
#define BLOCK_SIZE 5
#define BLOCKS ((WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE))
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 0.20
#define IMAGE_DIFF_THRESHOLD 0.002
#define DECREMENT_START 2
#define CROP_FACTOR 2.0

#define SLEEP_DURATION_SECONDS 43200 // 12 hours
#define SLEEP_HOUR 19 // Time in Kenya

#endif // CONSTANTS_H