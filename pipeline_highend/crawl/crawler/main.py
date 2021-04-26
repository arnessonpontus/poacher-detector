import time
from ftplib import FTP
import sys
sys.path.append('/Users/pontusarnesson/Documents/Skola/femman/exjobb/exjobb/poacher-detector/pipeline_highend')
import config
from PIL import Image, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True
from io import BytesIO
import tensorflow as tf
import numpy as np
from object_detection.utils import label_map_util
from object_detection.utils import visualization_utils as viz_utils
from dateutil import parser
from datetime import datetime
import cv2

detect_fn = None
category_index = None

use_90_class = True

def handle_FTP_connection(connection):
    try:
        connection.voidcmd("NOOP")
        retry = False
    except IOError as e:
        print("I/O error({0}): {1}".format(e.errno, e.strerror))
        retry = True

    while (retry):
        try:
            connection = FTP(config.FTP_HOST, config.FTP_USER, config.FTP_PASS)
            retry = False
        except IOError as e:
            print("Failed to connect to FTP, retrying...")
            print("I/O error({0}): {1}".format(e.errno, e.strerror))
            time.sleep(1)

    return connection

def run_inference(image):
    image_np = np.array(image)

    input_tensor = tf.convert_to_tensor(image_np)
    input_tensor = input_tensor[tf.newaxis, ...]

    detections = detect_fn(input_tensor)

    person_indices = detections['detection_classes'] == 1 if use_90_class else 4

    if np.any(person_indices): 
      max_score = np.max(detections['detection_scores'][person_indices])
    else:
      max_score = 0

    num_detections = int(detections.pop('num_detections'))

    detections = {key: value[0, :num_detections].numpy()
                    for key, value in detections.items()}
    detections['num_detections'] = num_detections

    detections['detection_classes'] = detections['detection_classes'].astype(np.int64)

    image_np_with_detections = image_np.copy()

    viz_utils.visualize_boxes_and_labels_on_image_array(
            image_np_with_detections,
            detections['detection_boxes'],
            detections['detection_classes'],
            detections['detection_scores'],
            category_index,
            use_normalized_coordinates=True,
            max_boxes_to_draw=200,
            min_score_thresh=.50,
            agnostic_mode=False)

    image = image_np_with_detections

    return image, max_score

if __name__ == "__main__":
    PATH_TO_SAVED_MODEL = "../../models/model_resnet_pretrained/saved_model"
    PATH_TO_LABELS = "../../models/label_map_90_class.pbtxt" if use_90_class else "../../models/label_map_8_class.pbtxt"

    print('Loading model...', end='')
    start_time = time.time()

    detect_fn = tf.saved_model.load(PATH_TO_SAVED_MODEL)

    end_time = time.time()
    elapsed_time = end_time - start_time
    print('Done! Took {} seconds'.format(elapsed_time))

    category_index = label_map_util.create_category_index_from_labelmap(PATH_TO_LABELS,
                                                                    use_display_name=True)

    connection = FTP(config.FTP_HOST, config.FTP_USER, config.FTP_PASS, timeout=60)

    last_detection_time = datetime.min
    detection_counter = 0
    while(True):
        connection = handle_FTP_connection(connection)

        filenames = connection.nlst(config.FTP_DIR)

        for filename in sorted(filenames):
            short_filename = filename.split("/")[-1]
            print("processing image ", short_filename)
            img_data = BytesIO()
            connection.retrbinary("RETR " + filename, img_data.write)
            image = Image.open(img_data)

            image, max_score = run_inference(image)

            timestamp = connection.voidcmd("MDTM " + filename)[4:].strip()
            upload_time = parser.parse(timestamp)
            
            if max_score > 0.5:
                time_since_detetion = (upload_time - last_detection_time).total_seconds()
                print("time since detection: ", time_since_detetion)
                if time_since_detetion > 60:
                    detection_counter = 0
                detection_counter += 1
                
                if detection_counter == 2:
                    print("****** Sending to FTP as: {} ******".format(short_filename))
                    
                    img = Image.fromarray(image)
                    byte_img = BytesIO()
                    img.save(byte_img, format="JPEG")
                    byte_img.seek(0)
                    connection.storbinary('STOR /thesis-highend/{}'.format(short_filename), byte_img)
                last_detection_time = upload_time

            img = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            cv2.imshow('image', img)
            cv2.waitKey(1)

            connection.rename(filename, '/thesis-highend-processed/{}'.format(short_filename))

        time.sleep(10)
