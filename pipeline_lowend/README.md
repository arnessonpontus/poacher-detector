# Low-end pipeline

## Overview

This pipeline runs the model inference on the ESP32 itself, using Tensorflow Lite Micro. Information about training a model can be found [here](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/micro/examples/person_detection/training_a_model.md). The code on the ESP32 runs on two cores in parallel, explained below.

### Core 1

This code will detect motion on the camera using a simple background subtraction algorithm. If it detects motion it will try to find a region of interest where the moving object is located in the image and crop this area. Finally, it resizes the cropped portion to the correct input size of the model.

### Core 2

The code on this core is only triggered if a region of interest (RoI) has been found on Core 1. It will then feed the cropped and resized RoI into the Tensorflow model to predict whether it is a person or not.

## How to run

- Make sure to have esp-idf installed and ready, more info on how to set up can be found [here](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).
- Remove the "example" from `secrets.example.h` in client/main and specify the values. Then move into client directory and run:

```bash
idf.py -p [PORT] monitor flash
```

## Event creation

With current config, if a person is detected twice within 10 seconds, an image is uploaded to the FTP server to create an event in the dashboard.

## Evaluation

`evaluate_model.cc` and `evaluate_pipeline.cc`can be used to evaluate the model from images that are stored on SD card. The first file will create the confusion matrix for the model on a sequence of images. The second file will check to see if an event is created from a sequence of images.
