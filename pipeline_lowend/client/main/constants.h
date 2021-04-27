#ifndef CONSTANTS_H
#define CONSTANTS_H

#define MODEL_INPUT_W 96
#define MODEL_INPUT_H 96
#define WIDTH 320
#define HEIGHT 240
#define NUM_CHANNELS 1
#define BLOCK_SIZE 5
#define BLOCKS ((WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE))
#define W (WIDTH / BLOCK_SIZE)
#define H (HEIGHT / BLOCK_SIZE)
#define BLOCK_DIFF_THRESHOLD 0.20
#define IMAGE_DIFF_THRESHOLD 0.003
#define DECREMENT_START 2
#define CROP_FACTOR 2.0

#define PERSON_THRESH 180

#endif // CONSTANTS_H

//When the ESP32_cam should start streaming. Military time.
#define WAKE_UP_TIME_ 8

//When the ESP32_cam should end streaming. Military time.
#define SLEEP_TIME_ 16

//Defines time zone by setting the offset in seconds from GMT time.
#define GMT_OFFSET_SEC_ 3600

//Defines summer/winter time (in secconds). 0 = winter, 3600 = summer.
#define DAYLIGHT_OFFSET_SEC_ 0

//The server to fetch time.
#define NTP_ "pool.ntp.org"

#define uS_TO_HOUR_ 3600000000ULL
#define uS_TO_MINUTE_ 60000000ULL
#define uS_TO_SECONDS_ 1000000ULL
#define WIFI_TIMEOUT_ 120000
#define MS_PER_HOUR_ 3600000