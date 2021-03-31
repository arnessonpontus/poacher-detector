#include <ArduinoWebsockets.h>

#include "esp_camera.h"
#include <WiFi.h>

#include "camera_pins.h"

const char* ssid = "Pone Plus";
const char* password = "Sutnop123";
const char* websocket_server_host = "192.168.243.93";
const uint16_t websocket_server_port = 8888;

#define FRAME_SIZE FRAMESIZE_VGA
#define PIXFORMAT PIXFORMAT_JPEG
#define WIDTH 640
#define HEIGHT 480
#define BLOCK_SIZE 10
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 0.15
#define IMAGE_DIFF_THRESHOLD 0.03
#define DEBUG 0

uint16_t *prev_frame;
uint16_t *current_frame;

using namespace websockets;
WebsocketsClient client;

bool downsample(uint8_t* rgb_img);
bool motion_detect();
void update_frame();
void flash_led();

void onEventsCallback(WebsocketsEvent event, String data) {
    if(event == WebsocketsEvent::ConnectionOpened) {
        Serial.println("Connnection Opened");
    } else if(event == WebsocketsEvent::ConnectionClosed) {
        Serial.println("Connnection Closed");
        esp_restart();
    } else if(event == WebsocketsEvent::GotPing) {
        Serial.println("Got a Ping!");
    } else if(event == WebsocketsEvent::GotPong) {
        Serial.println("Got a Pong!");
    }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  prev_frame = (uint16_t *)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  current_frame = (uint16_t *)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);

  pinMode(4, OUTPUT); // Set led pin

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
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT;
  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAME_SIZE;
    config.jpeg_quality = 40;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAME_SIZE;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  client.onEvent(onEventsCallback);
  
  while (!client.connect(websocket_server_host, websocket_server_port, "/"))
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Websocket Connected!");
  
  client.onMessage([&](WebsocketsMessage message){
    Serial.print("Human detected on server");
    flash_led(50);
  });
}

void loop() {
  camera_fb_t *frame_buffer = esp_camera_fb_get();

  if (!frame_buffer)
    return;

  uint8_t* rgb_img = (uint8_t*) heap_caps_malloc(WIDTH * HEIGHT * 3, MALLOC_CAP_SPIRAM);
  
  fmt2rgb888(frame_buffer->buf, frame_buffer->len, PIXFORMAT, rgb_img);

  if (!downsample(rgb_img)) {
    Serial.println("Failed capture");
    delay(3000);
    
    return;
  }
  
  if (motion_detect()) {
    Serial.println("Motion detected");

    client.sendBinary((const char*) frame_buffer->buf, frame_buffer->len);
  }
  
  if(client.available()) {
    client.poll();
  }
  
  update_frame();
  Serial.println("=================");

  esp_camera_fb_return(frame_buffer);
  heap_caps_free(rgb_img);
}

bool downsample(uint8_t* rgb_img) {
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
    const uint8_t pixel = rgb_img[i*3]; // Only red channel

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

/**
 * Compute the number of different blocks
 * If there are enough, then motion happened
 */
bool motion_detect() {
  uint16_t changes = 0;
  const uint16_t blocks = (WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE);

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint16_t i = x + y * W;
      float current = current_frame[i];
      float prev = prev_frame[i];
      float delta = abs(current - prev) / prev;

      if (delta >= BLOCK_DIFF_THRESHOLD) {
        changes += 1;
      }
    }
  }

  Serial.print("Changed ");
  Serial.print(changes);
  Serial.print(" out of ");
  Serial.println(blocks);

  return (1.0 * changes / blocks) > IMAGE_DIFF_THRESHOLD;
}

/**
 * Copy current frame to previous
 */
void update_frame() {
  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      uint16_t i = x + y * W;
      prev_frame[i] = current_frame[i];
    }
  }
}

void flash_led(unsigned long ms) {
  digitalWrite(4, HIGH); //Turn on led
  delay(ms); // Wait ms
  digitalWrite(4, LOW); //Turn off led
}
