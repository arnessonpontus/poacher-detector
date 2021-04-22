# Pipeline Highend

## How to run

### Crawler (backend)

- From crawl/crawler, run:

```bash
python3 main.py
```

### Client

- Make sure to have esp-idf installed and ready, more info on how to set up can be found [here](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).
- Remove the "example" from `secrets.example.h` in client/main and specify the values. Then move into client directory and run:

```bash
idf.py -p [PORT] monitor flash
```

## Overview

This pipeline runs only the image capturing and motion detection on the ESP32-CAM. If there is any motion, images will be sent to an FTP. A Pyhton script will then crawl that FTP folder and run inference on all new images. If there is a person in the image, that image will be put in another FPT folder and sent to the dashboard. All processed images will be stored in a FTP folder for later evaluation and training.

## ws

This is currenly not used, "crawl" is used as high-end pipeline instead.

## crawl

Crawl is the currently used high-end pipeline which contains the client, that is run on the ESP32-CAM, and the crawler which handles both the FTP crawling, and runs the inference.

### crawler

This code will crawl a specific folder on the FTP server with a set interval. If the folder contains any file, it will download that file and run inference on it with a model located in /models.
Currently, if any detection has a higher score that 0.5 for the class "person" the image will be sent to the FTP folder where images gets uploaded as reports. The image will also be sent to a folder to be saved for later evaluation, and removed from the initial folder.

### Client

This code runs on the ESP32-CAM and is used to send relevant images to the FTP server that is being crawled by the crawler. Since no inference is performed on the client side in this pipeline, this code only runs on one core.

After the image capture, motion will be detected using a simple background subtraction algorithm. If more than a certain threshold of motion is observed, the image will be sent to the FTP server.

## Models

Models contains all pretrained and transfer learned tensorflow models.

## Evaluate

`evaluate_model.py` and `evaluate_pipeline.py`can be used to evaluate the model from images that are stored locally on the computer. The first file will create the confusion matrix for the model on a sequence of images. The second file will check to see if an event is created from a sequence of images.
