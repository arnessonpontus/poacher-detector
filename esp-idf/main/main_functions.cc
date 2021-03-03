/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "main_functions.h"
#include "constants.h"

#define MOUNT_POINT "/sdcard"

// DMA channel to be used by the SPI peripheral
#ifndef SPI_DMA_CHAN
#define SPI_DMA_CHAN 1
#endif //SPI_DMA_CHAN

#define MAXIMUM(x, y) (((x) > (y)) ? (x) : (y))
#define MINIMUM(x, y) (((x) < (y)) ? (x) : (y))

static const char *TAG = "MAIN_FUNCTIONS";
uint16_t *prev_frame;
uint16_t *current_frame;
uint8_t *bg_image;

void crop_image_center(uint8_t *src, uint8_t *dst)
{
  int center_x = WIDTH / 2;

  int i_p1_x = center_x - HEIGHT / 2;
  int i_p1_y = 0;

  int i_p2_x = center_x + HEIGHT / 2;
  int i_p2_y = HEIGHT;

  int counter = 0;

  for (int i = 0; i < WIDTH * HEIGHT; i++)
  {
    uint16_t x = i % WIDTH;
    uint16_t y = floor(i / WIDTH);

    if (x >= i_p1_x && x < i_p2_x && y >= i_p1_y && y < i_p2_y)
    {
      dst[counter] = src[i];
      counter++;
    }
  }
}

void crop_image(uint8_t *src, uint8_t *dst, uint16_t changes, uint32_t &cropped_len, uint16_t &accumelated_x, uint16_t &accumelated_y)
{
  uint16_t diff_sum_x = 0;
  uint16_t diff_sum_y = 0;
  float mean_x = (float)accumelated_x / changes;
  float mean_y = (float)accumelated_y / changes;

  //uint8_t *bg_mask_binary = (uint8_t *)heap_caps_malloc(W * H, MALLOC_CAP_SPIRAM);

  for (int j = 0; j < W * H; j++)
  {
    if (bg_image[j] > 0)
    {
      //bg_mask_binary[j] = 255;
      diff_sum_x += abs(j % W - mean_x);
      diff_sum_y += abs(floor(j / W) - mean_y);
    }
    else
    {
      //bg_mask_binary[j] = 0;
    }
  }

  float variance_x = ((float)diff_sum_x / changes);
  float variance_y = ((float)diff_sum_y / changes);

  float half_width = MAXIMUM(variance_x, variance_y) * CROP_FACTOR;

  // Mult by 10 to get pixel in original img
  mean_x *= 10;
  mean_y *= 10;
  half_width *= 10;
  half_width = MINIMUM(half_width, (float)(HEIGHT / 2));

  // Shift crop towards head
  mean_y -= 0.5 * half_width;

  float p1_x = (mean_x - half_width);
  float p1_y = (mean_y - half_width);

  float p2_x = (mean_x + half_width);
  float p2_y = (mean_y + half_width);

  // Shift if square is outside image border
  if (p1_x < 0)
  {
    p1_x = 0;
    p2_x = 2 * half_width;
  }

  if (p1_y < 0)
  {
    p1_y = 0;
    p2_y = 2 * half_width;
  }

  if (p2_x > WIDTH)
  {
    p2_x = WIDTH;
    p1_x = WIDTH - 2 * half_width;
  }

  if (p2_y > HEIGHT)
  {
    p2_y = HEIGHT;
    p1_y = HEIGHT - 2 * half_width;
  }

  int i_p1_x = (int)p1_x;
  int i_p1_y = (int)p1_y;

  int i_p2_x = (int)p2_x;
  int i_p2_y = (int)p2_y;

  if (i_p2_x - i_p1_x > i_p2_y - i_p1_y)
  {
    i_p2_y++;
  }
  else if (i_p2_x - i_p1_x < i_p2_y - i_p1_y)
  {
    i_p2_x++;
  }

  for (int i = 0; i < WIDTH * HEIGHT; i++)
  {
    uint16_t x = i % WIDTH;
    uint16_t y = floor(i / WIDTH);

    if (x >= i_p1_x && x < i_p2_x && y >= i_p1_y && y < i_p2_y)
    {
      dst[cropped_len] = src[i];
      cropped_len++;
    }
  }

  //heap_caps_free(bg_mask_binary);
}

void bg_subtraction(uint16_t &changes, uint16_t &accumelated_x, uint16_t &accumelated_y)
{
  changes = 0;

  accumelated_x = 0;
  accumelated_y = 0;

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      uint16_t i = x + y * W;
      float current = current_frame[i];
      float prev = prev_frame[i];
      float delta = abs(current - prev) / prev;

      if (delta >= BLOCK_DIFF_THRESHOLD)
      {
        changes += 1;
        bg_image[i] = DECREMENT_START;
        accumelated_x += i % W;
        accumelated_y += floor(i / W);
      }
      else
      {
        if (bg_image[i] > 0)
          bg_image[i] = bg_image[i] - 1;
      }
    }
  }

  ESP_LOGI(TAG, "Changed %d", changes);
  ESP_LOGI(TAG, "out of %d blocks", BLOCKS);
}

void update_frame()
{
  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      uint16_t i = x + y * W;
      prev_frame[i] = current_frame[i];
    }
  }
}

bool downscale(uint8_t *image)
{
  // set all 0s in current frame
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
    {
      uint16_t i = x + y * W;
      current_frame[i] = 0;
    }

  // down-sample image in blocks
  for (uint32_t i = 0; i < WIDTH * HEIGHT; i++)
  {
    const uint16_t x = i % WIDTH;
    const uint16_t y = floor(i / WIDTH);
    const uint8_t block_x = floor(x / BLOCK_SIZE);
    const uint8_t block_y = floor(y / BLOCK_SIZE);
    uint16_t j = block_x + block_y * W;
    const uint8_t pixel = image[i];

    // average pixels in block (accumulate)
    current_frame[j] += pixel;
  }

  // average pixels in block (rescale)
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
    {
      uint16_t i = x + y * W;
      current_frame[i] /= BLOCK_SIZE * BLOCK_SIZE;
    }

  return true;
}

void setup_sdcard()
{
  esp_err_t ret;
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024};
  sdmmc_card_t *card;
  const char mount_point[] = MOUNT_POINT;
  ESP_LOGI(TAG, "Initializing SD card");

  ESP_LOGI(TAG, "Using SDMMC peripheral");
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();

  // This initializes the slot without card detect (CD) and write protect (WP) signals.
  // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

  // To use 1-line SD mode, uncomment the following line:
  slot_config.width = 1;

  gpio_set_pull_mode((gpio_num_t)15, GPIO_PULLUP_ONLY); // CMD, needed in 4- and 1- line modes
  gpio_set_pull_mode((gpio_num_t)2, GPIO_PULLUP_ONLY);  // D0, needed in 4- and 1-line modes
  //gpio_set_pull_mode((gpio_num_t)4, GPIO_PULLUP_ONLY);  // D1, needed in 4-line mode only
  //gpio_set_pull_mode((gpio_num_t)12, GPIO_PULLUP_ONLY); // D2, needed in 4-line mode only
  gpio_set_pull_mode((gpio_num_t)13, GPIO_PULLUP_ONLY); // D3, needed in 4- and 1-line modes

  ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK)
  {
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount filesystem. "
                    "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                    "Make sure SD card lines have pull-up resistors in place.",
               esp_err_to_name(ret));
    }
    return;
  }

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, card);
}

void save_to_sdcard(uint8_t *image, size_t len, char filename[])
{
  ESP_LOGI(TAG, "Opening file");

  FILE *f = fopen(filename, "w");
  if (f == NULL)
  {
    ESP_LOGE(TAG, "Failed to open file for writing");
    return;
  }
  fwrite(image, 1, len, f);
  fflush(f);
  fclose(f);
  ESP_LOGI(TAG, "File written to %s", filename);
}

void get_stored_image(uint8_t* input_image, uint16_t image_number) {
  char buf[0x100];
  snprintf(buf, sizeof(buf), "/sdcard/esp/%04d.bin", image_number);
  FILE *f = fopen(buf, "r");
  if (f == NULL)
  {
    ESP_LOGE(TAG, "Failed to open file for reading %d", errno);
    return;
  }

  fread(input_image, WIDTH * HEIGHT, 1,  f);
  fflush(f);
  fclose(f);
  ESP_LOGI(TAG, "File read");
}

void setup_mf() {
  ESP_ERROR_CHECK(nvs_flash_init());

  prev_frame = (uint16_t *)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  current_frame = (uint16_t *)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  bg_image = (uint8_t *)heap_caps_malloc(W * H * sizeof(uint8_t), MALLOC_CAP_SPIRAM);

  for (int i = 0; i < W * H; i++)
  {
    prev_frame[i] = 0;
    current_frame[i] = 0;
    bg_image[i] = 0;
  }

  setup_sdcard();
}
