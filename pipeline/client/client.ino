#define CAMERA_MODEL_AI_THINKER

#include "esp_camera.h"
#include "camera_pins.h"
#include "image_util.h"

#include <FS.h>
#include "SD_MMC.h"

#define FRAME_SIZE FRAMESIZE_QVGA
#define WIDTH 320
#define HEIGHT 240
#define BLOCK_SIZE 10
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 0.2
#define IMAGE_DIFF_THRESHOLD 0.1

uint16_t prev_frame[H][W] = { 0 };
uint16_t current_frame[H][W] = { 0 };
camera_fb_t *frame_buffer;
uint16_t pictureNumber = 0;

bool setup_camera(framesize_t);
bool capture_still();
bool motion_detect();
void update_frame();

/**
 *
 */
void setup() {
    Serial.begin(115200);
    Serial.println(setup_camera(FRAME_SIZE) ? "OK" : "ERR INIT");

    if (!SD_MMC.begin("/sdcard", true))
    {
      Serial.println("SD Card Mount Failed");
      return;
    }
  
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE)
    {
      Serial.println("No SD Card attached");
      return;
    }
}

/**
 *
 */
void loop() {
    if (!capture_still()) {
        Serial.println("Failed capture");
        delay(3000);

        return;
    }

    if (motion_detect()) {
        Serial.println("Motion detected");
    }

    update_frame();
    Serial.println("=================");
    esp_camera_fb_return(frame_buffer);
}


/**
 *
 */
bool setup_camera(framesize_t frameSize) {
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = frameSize;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    bool ok = esp_camera_init(&config) == ESP_OK;

    sensor_t *sensor = esp_camera_sensor_get();
    sensor->set_framesize(sensor, frameSize);

    return ok;
}

/**
 * Capture image and do down-sampling
 */
bool capture_still() {
    frame_buffer = esp_camera_fb_get();

    if (!frame_buffer)
        return false;

    // set all 0s in current frame
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            current_frame[y][x] = 0;


    // down-sample image in blocks
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
        const uint16_t x = i % WIDTH;
        const uint16_t y = floor(i / WIDTH);
        const uint8_t block_x = floor(x / BLOCK_SIZE);
        const uint8_t block_y = floor(y / BLOCK_SIZE);
        const uint8_t pixel = frame_buffer->buf[i];
        const uint16_t current = current_frame[block_y][block_x];

        // average pixels in block (accumulate)
        current_frame[block_y][block_x] += pixel;
    }

    // average pixels in block (rescale)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            current_frame[y][x] /= BLOCK_SIZE * BLOCK_SIZE;

    return true;
}

/**
 * Compute the number of different blocks
 * If there are enough, then motion happened
 */
bool motion_detect() {
    uint16_t changes = 0;
    const uint16_t blocks = (WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE);
    
    uint16_t accumelated_x = 0;
    uint16_t accumelated_y = 0;
    float mean_x = 0;
    float mean_y = 0;
    uint16_t diff_sum_x = 0;
    uint16_t diff_sum_y = 0;
    float variance_x = 0;
    float variance_y = 0;
    uint16_t pixel_counter = 0;

    char *bg_image_str = (char *)heap_caps_malloc(W*H + 1, MALLOC_CAP_SPIRAM);
    uint8_t* cropped_img = (uint8_t*) heap_caps_malloc(WIDTH*HEIGHT, MALLOC_CAP_SPIRAM);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t i = x + y * W;
            float current = current_frame[y][x];
            float prev = prev_frame[y][x];
            float delta = abs(current - prev) / prev;

            if (delta >= BLOCK_DIFF_THRESHOLD) {
                changes += 1;
                bg_image_str[i] = '0';
                pixel_counter++;
                accumelated_x += i % W;
                accumelated_y += floor(i / W);
            } else {
                bg_image_str[x + y * W] = '1';
            }
        }
    }

    if (pixel_counter < 10) {
      heap_caps_free(cropped_img);
      heap_caps_free(bg_image_str);
      return false;
    }
    
    mean_x = (float) accumelated_x/pixel_counter;
    mean_y = (float) accumelated_y/pixel_counter;
    
    for (int j = 0; j < W * H; j++) {
      if (bg_image_str[j] == '0') {
        diff_sum_x += pow((j % 32 - mean_x), 2);
        diff_sum_y += pow((floor(j / 32) - mean_y), 2);
      }
    }

    variance_x = (float) diff_sum_x / pixel_counter;
    variance_y = (float) diff_sum_y / pixel_counter;

    float half_width = max(variance_x, variance_y) * 1.2;

    // Mult by 10 to get pixel in original img
    mean_x *= 10;
    mean_y *= 10;
    half_width *= 10;
    half_width = min(half_width, (float) (HEIGHT / 2));

    float p1_x = (mean_x - half_width);
    float p1_y = (mean_y - half_width);
    
    float p2_x = (mean_x + half_width);
    float p2_y = (mean_y + half_width);

    // Shift if square is outside image border
    if (p1_x < 0) {
      p1_x = 0;
      p2_x = 2 * half_width;
    }
    
    if (p1_y < 0) {
       p1_y = 0;
       p2_y = 2 * half_width;
    }

    if (p2_x > WIDTH) {
       p2_x = WIDTH;
       p1_x = WIDTH - 2 * half_width;
    }

    if (p2_y > HEIGHT) {
      p2_y = HEIGHT;
      p1_y = HEIGHT - 2 * half_width;
    }

    int i_p1_x = (int) p1_x;
    int i_p1_y = (int) p1_y;
    
    int i_p2_x = (int) p2_x;
    int i_p2_y = (int) p2_y;

    if (i_p2_x - i_p1_x > i_p2_y - i_p1_y) {
      i_p2_y++;
    } else if (i_p2_x - i_p1_x < i_p2_y - i_p1_y) {
      i_p2_x++;
    }

    uint16_t counter = 0;
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
      uint16_t x = i % WIDTH;
      uint16_t y = floor(i / WIDTH);
   
      if (x >= i_p1_x && x < i_p2_x && y >= i_p1_y && y < i_p2_y) {
        cropped_img[counter] = frame_buffer->buf[i];
        counter++;
      }
    }
    
    uint8_t* jpeg;
    size_t len;

    uint8_t* resized_img = (uint8_t*) heap_caps_malloc(96 * 96, MALLOC_CAP_SPIRAM);

    image_resize_linear(resized_img, cropped_img, 96, 96, 1, sqrt(counter), sqrt(counter));

    fmt2jpg(resized_img, 96 * 96, 96, 96, PIXFORMAT_GRAYSCALE, 90, &jpeg, &len);
    
    //bg_image_str[W*H] = '\0';

    fs::FS &fs = SD_MMC;
    char buf[0x100];
    snprintf(buf, sizeof(buf), "/esp/images/esp_picture%04d.jpg", ++pictureNumber);
  
    File file = fs.open(buf, FILE_WRITE);
    if (!file)
    {
      Serial.println("Failed to open file in writing mode");
    }
  
    Serial.println("Writing to file " + (String)buf);
  
    file.write(jpeg, len);
  
    file.close();

    Serial.print("Changed ");
    Serial.print(changes);
    Serial.print(" out of ");
    Serial.println(blocks);

    heap_caps_free(jpeg);
    heap_caps_free(cropped_img);
    heap_caps_free(bg_image_str);
    heap_caps_free(resized_img);
    return (1.0 * changes / blocks) > IMAGE_DIFF_THRESHOLD;
}

/**
 * Copy current frame to previous
 */
void update_frame() {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            prev_frame[y][x] = current_frame[y][x];
        }
    }
}
