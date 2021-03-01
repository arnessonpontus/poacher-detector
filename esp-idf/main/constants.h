#ifndef CONSTANTS_H
#define CONSTANTS_H

#define MODEL_INPUT_W 96
#define MODEL_INPUT_H 96
#define WIDTH 320
#define HEIGHT 240
#define BLOCK_SIZE 10
#define BLOCKS ((WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE))
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 0.25
#define IMAGE_DIFF_THRESHOLD 0.007

#endif // CONSTANTS_H