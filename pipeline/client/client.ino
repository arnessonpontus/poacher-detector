#define CAMERA_MODEL_AI_THINKER

#include <ArduinoWebsockets.h>
//#include <JPEGDEC.h>
#include <FS.h>
#include "SD_MMC.h"
#include <EloquentArduino.h>
#include <eloquentarduino/io/serial_print.h>
#include <eloquentarduino/vision/camera/ESP32Camera.h>
#include <eloquentarduino/vision/io/writers/JpegWriter.h>
#include <Preferences.h>
#include "RGB565Decoder.h"
//#include <TJpg_Decoder.h>
#include <JPEGDecoder.h>


#define FRAME_SIZE FRAMESIZE_VGA
#define PIXFORMAT PIXFORMAT_JPEG
#define W 640
#define H 480
#define w 64
#define h 48
#define DIFF_THRESHOLD 15
#define MOTION_THRESHOLD 0.05
#define FLASH_LENGTH 500
#define LED_PIN 33

// delete the second definition if you want to turn on code benchmarking
#define timeit(label, code)                                               \
  {                                                                       \
    uint32_t start = millis();                                            \
    code;                                                                 \
    uint32_t duration = millis() - start;                                 \
    eloquent::io::print_all("It took ", duration, " millis for ", label); \
  }
#define timeit(label, code) code;

const char *ssid = "PonePlus";
const char *password = "Sutnop123";
const char *websocket_server_host = "192.168.194.93";
const uint16_t websocket_server_port = 8888;

using namespace websockets;
using namespace Eloquent::Vision;

WebsocketsClient client;

camera_fb_t *frame;
uint8_t *rgb = NULL;
Camera::ESP32Camera camera(PIXFORMAT);
uint8_t downscaled[w * h];
RGB565Decoder decoder;
//IO::Decoders::GrayscaleRandomAccessDecoder decoder;
//IO::Decoders::Red565RandomAccessDecoder decoder;
Processing::Downscaling::Center<W / w, H / h> strategy;
Processing::Downscaling::Downscaler<W, H, w, h> downscaler(&decoder, &strategy);
Processing::MotionDetector<w, h> motion;
unsigned int pictureNumber = 0;
unsigned long triggered_ms = 0;
unsigned long pixel_counter = 0;
Preferences preferences;
//JPEGDEC jpeg;

void capture();
void save();
void stream_downscaled();
void stream();
void setup_connection();
//int drawMCU(JPEGDRAW *pDraw);
//bool drawMCU(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t* bitmap);

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Begin");

  //rgb = (uint8_t*) malloc(W * H * 3);
  /*
  if (rgb == NULL) {
    Serial.println("Null ptr");
    return;
  }
  */
  /*
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(drawMCU);
  */
  
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
  // set how much a pixel value should differ to be considered as a change
  motion.setDiffThreshold(DIFF_THRESHOLD);
  // set how many pixels (in percent) should change to be considered as motion
  motion.setMotionThreshold(MOTION_THRESHOLD);
  // prevent consecutive triggers
  motion.setDebounceFrames(2);
}

void loop()
{
  unsigned long current_ms = millis();

  timeit("Capture and convert", capture());
  
  eloquent::io::print_all(motion.changes(), " pixels changed");

  if (motion.triggered())
  {
    Serial.println("Motion detected");

    digitalWrite(LED_PIN, LOW);
    triggered_ms = current_ms;

    //timeit("Save to SD card", save(jpeg, len));

    //client.sendBinary((const char *)jpeg, len);
  }

  Serial.println("YYAAAAAYYYYYY, DONE!");
  pixel_counter = 0;
  
  esp_camera_fb_return(frame);
  
  if (current_ms - triggered_ms >= FLASH_LENGTH)
  {
    digitalWrite(LED_PIN, HIGH);
  }

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

    if (connection_attempts > 2) {
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

  void* ptrVal = NULL;
  uint32_t ARRAY_LENGTH = frame->width * frame->height * 2;
  
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < ARRAY_LENGTH) {
    Serial.println("No memory!");
  }

  ptrVal = heap_caps_malloc(ARRAY_LENGTH, MALLOC_CAP_SPIRAM);
  rgb = (uint8_t*) ptrVal;
  
  /*
  jpeg.openRAM((uint8_t*) frame->buf, frame->len, drawMCU);
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  jpeg.decode(0, 0, JPEG_SCALE_HALF);
  jpeg.close();
  */

  //TJpgDec.drawJpg(0, 0, frame->buf, frame->len);

  JpegDec.decodeArray(frame->buf, frame->len);
  // Create a header packet with info about the image
  String header = "$ITHDR,";
  header += JpegDec.width;
  header += ",";
  header += JpegDec.height;
  header += ",";
  header += JpegDec.MCUSPerRow;
  header += ",";
  header += JpegDec.MCUSPerCol;
  header += ",";
  header += "minJpeg";
  header += ",";
  header.toCharArray((char*) rgb, 240);
  uint16_t *pImg;

  while(JpegDec.read()) {
    pImg = JpegDec.pImage;
    uint16_t color;
    uint8_t i = 6;

    int mcuXCoord = JpegDec.MCUx;
    int mcuYCoord = JpegDec.MCUy;

    uint32_t mcuPixels = JpegDec.MCUWidth * JpegDec.MCUHeight;

    // Repeat for all pixels in the current MCU
    while(mcuPixels--) {
      // Read the color of the pixel as 16-bit integer
      color = *pImg++;
      
      // Split it into two 8-bit integers
      rgb[i] = color >> 8;
      rgb[i+1] = color;
      i += 2;
    }
  }

  
  size_t len;
  uint8_t *jpeg_image;
  timeit("Jpeg conversion", fmt2jpg(rgb, W * H, W, H, PIXFORMAT_RGB565, 30, &jpeg_image, &len));
  timeit("Save to SD card", save(jpeg_image, len));

  Serial.println("Captured");

  // scale image from size H * W to size h * w
  //timeit("downscale", downscaler.downscale(rgb, downscaled));
  //Serial.println("R from downscaler");

  //downscaler.downscale(rgb, downscaled);

  // detect motion on the downscaled image
  timeit("motion detection", motion.detect(downscaled));

  heap_caps_free(ptrVal);
  heap_caps_free(jpeg_image);
}

void save(uint8_t *jpeg_image, size_t len)
{
  fs::FS &fs = SD_MMC;
  String path = "/esp/images/esp_picture" + String(pictureNumber) + ".jpg";

  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file in writing mode");
  }
  else
  {
    file.write(jpeg_image, len);
    Serial.printf("Saved file to path: %s\n", path.c_str());
    preferences.putUInt("camera_counter", ++pictureNumber);
  }
  file.close();
}
/*
bool drawMCU(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t* bitmap) {
  for (int i = 0; i < width * height; i++) { 
    rgb[pixel_counter + i] = bitmap[i];
    //rgb[pixel_counter + i * 2] = bitmap[i] >> 8;
    //rgb[pixel_counter + i * 2 + 1] = bitmap[i];
  }
  pixel_counter += width * height;
  return 1;
}
*/

/*
int drawMCU(JPEGDRAW *pDraw) {
  for (int i = 0; i < pDraw->iWidth * pDraw->iHeight; i++) {
      
    //uint8_t b = (pDraw->pPixels[i] & 0x001F) << 3;
    //uint8_t g = (pDraw->pPixels[i] & 0x07E0) >> 3;
    //uint8_t r = (pDraw->pPixels[i] & 0xF800) >> 8;
    
    //rgb[pixel_counter + i] = r;
    //rgb[pixel_counter + i] = pDraw->pPixels[i];
    
    //rgb[pixel_counter + i * 3] = r;
    //rgb[pixel_counter + i * 3 + 1] = g;
    //rgb[pixel_counter + i * 3 + 2] = b;
    
    rgb[pixel_counter + i * 2] = pDraw->pPixels[i] >> 8;
    rgb[pixel_counter + i * 2 + 1] = pDraw->pPixels[i];

  }
  pixel_counter += pDraw->iWidth * pDraw->iHeight;
}
*/
