#define CAMERA_MODEL_AI_THINKER

#include <ArduinoWebsockets.h>

#include <FS.h>
#include "SD_MMC.h"
#include <EloquentArduino.h>
#include <eloquentarduino/io/serial_print.h>
#include <eloquentarduino/vision/camera/ESP32Camera.h>
#include <eloquentarduino/vision/io/writers/JpegWriter.h>
#include <Preferences.h>

#define FRAME_SIZE FRAMESIZE_VGA
#define PIXFORMAT PIXFORMAT_GRAYSCALE
#define W 640
#define H 480
#define w 64
#define h 48
#define DIFF_THRESHOLD 15
#define MOTION_THRESHOLD 0.05
#define FLASH_LENGTH 500
#define LED_PIN 33
#define ADOPTION_RATE 2
#define THRESH 40

// delete the second definition if you want to turn on code benchmarking
#define timeit(label, code)                                               \
  {                                                                       \
    uint32_t start = millis();                                            \
    code;                                                                 \
    uint32_t duration = millis() - start;                                 \
    eloquent::io::print_all("It took ", duration, " millis for ", label); \
  }
//#define timeit(label, code) code;

const char *ssid = "Pone Plus";
const char *password = "Sutnop123";
const char *websocket_server_host = "192.168.62.93";
const uint16_t websocket_server_port = 8888;

using namespace websockets;
using namespace Eloquent::Vision;

WebsocketsClient client;

camera_fb_t *frame;
Camera::ESP32Camera camera(PIXFORMAT);
//uint8_t downscaled[w * h];
//IO::Decoders::GrayscaleRandomAccessDecoder decoder;
//IO::Decoders::Red565RandomAccessDecoder decoder;
//Processing::Downscaling::Center<W / w, H / h> strategy;
//Processing::Downscaling::Downscaler<W, H, w, h> downscaler(&decoder, &strategy);
//Processing::MotionDetector<w, h> motion;
unsigned int pictureNumber = 0;
unsigned int gray_number = 0;
unsigned long triggered_ms = 0;
Preferences preferences;
uint8_t *median;
bool is_first_frame = true;

void capture();
void save();
void stream_downscaled();
void stream();
void setup_connection();

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Begin");

  median = (uint8_t *)heap_caps_malloc(W * H, MALLOC_CAP_SPIRAM);

  preferences.begin("poach_det", false);
  pictureNumber = preferences.getUInt("camera_counter", 0);

  setup_connection();

  pinMode(LED_PIN, OUTPUT); // Set led pin

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

  camera.begin(FRAME_SIZE, 30, 10000000);
}

void loop()
{
  unsigned long current_ms = millis();
  Serial.println(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  capture();

  esp_camera_fb_return(frame);
  delay(30);
}

void setup_connection()
{
  WiFi.begin(ssid, password);

  uint8_t connection_attempts = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");

    if (connection_attempts > 2)
    {
      ESP.restart();
    }

    connection_attempts++;
  }

  Serial.println("");
  Serial.println("WiFi connected");

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  while (!client.connect(websocket_server_host, websocket_server_port, "/"))
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Websocket Connected!");
}

void capture()
{
  timeit("capture frame", frame = camera.capture());

  /*
  // Remove first bad images
  if (gray_number < 2)
  {
    gray_number++;
    return;
  }

  uint32_t ARRAY_LENGTH = frame->width * frame->height;

  uint8_t *gray_image = (uint8_t *)heap_caps_malloc(ARRAY_LENGTH, MALLOC_CAP_SPIRAM);
  memcpy(gray_image, frame->buf, frame->len);
  */
  uint32_t ARRAY_LENGTH = frame->width * frame->height;
  char *bg_image_str = (char *)heap_caps_malloc(ARRAY_LENGTH + 1, MALLOC_CAP_SPIRAM);
  /*

  if (is_first_frame == true)
  {
    is_first_frame = false;

    memcpy(median, gray_image, ARRAY_LENGTH * sizeof(uint8_t));
    heap_caps_free(gray_image);
    heap_caps_free(bg_image_str);
    return;
  }

  for (int i = 0; i < ARRAY_LENGTH; i++)
  {
    if (gray_image[i] > median[i])
    {
      median[i] += ADOPTION_RATE;
    }
    else
    {
      median[i] -= ADOPTION_RATE;
    }

    if (abs(gray_image[i] - median[i]) <= THRESH)
    {
      gray_image[i] = 1;
    }
    else
    {
      gray_image[i] = 0;
    }
  }
  */

  for (int i = 0; i < ARRAY_LENGTH; i++)
  {
    //bg_image_str[i] = gray_image[i] + '0';
    bg_image_str[i] = frame->buf[i] + '0';
  }

  bg_image_str[ARRAY_LENGTH] = '\0';

  fs::FS &fs = SD_MMC;
  char buf[0x100];
  snprintf(buf, sizeof(buf), "/esp/images/esp_picture%04d.txt", ++pictureNumber);

  File file = fs.open(buf, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file in writing mode");
  }

  Serial.println("Writing to file " + (String)buf);

  file.println(bg_image_str);

  file.close();

  /*
  if (gray_number > 3)
  {
    for (int i = 0; i < ARRAY_LENGTH; i++)
    {
      bg_image_str[i] = gray_image[i] + '0';
    }

    bg_image_str[ARRAY_LENGTH] = '\0';

    fs::FS &fs = SD_MMC;
    char buf[0x100];
    snprintf(buf, sizeof(buf), "/esp/images/esp_picture%04d.txt", ++pictureNumber);

    File file = fs.open(buf, FILE_WRITE);
    if (!file)
    {
      Serial.println("Failed to open file in writing mode");
    }

    Serial.println("Writing to file " + (String)buf);

    file.println(bg_image_str);

    file.close();
  }
  */
  gray_number++;

  //heap_caps_free(gray_image);
  heap_caps_free(bg_image_str);
}

void save(uint8_t *jpeg, size_t len)
{
  fs::FS &fs = SD_MMC;
  char buf[0x100];
  snprintf(buf, sizeof(buf), "/esp/images/esp_picture%03d.jpg", pictureNumber);

  File file = fs.open(buf, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file in writing mode");
  }
  else
  {
    file.write(jpeg, len);
    Serial.printf("Saved file to path: %s\n", buf);
    preferences.putUInt("camera_counter", ++pictureNumber);
  }
  file.close();
}
