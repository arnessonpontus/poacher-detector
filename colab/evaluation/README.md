# Run evaluation on motion detection model

The script eval_person_detector.ipynb will perform evaluation on a test dataset and output metrics such as mAP.

## Setup

- Download the desired model (eg. download.tensorflow.org/models/object_detection/tf2/20200711/centernet_hg104_1024x1024_coco17_tpu-32.tar.gz) and save it on Google Drive at /My Drive/models/MODEL_NAME.
- Update ```pipeline.config``` with correct ```num_classes``` and paths. See example in [pipeline.config](example_files/pipeline.config)
- Run all cells. Output is stored in MODEL_NAME/saved_model/eval/