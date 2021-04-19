import time
from ftplib import FTP
import sys
sys.path.append('/Users/pontusarnesson/Documents/Skola/femman/exjobb/exjobb/poacher-detector/pipeline_highend')
import config
from PIL import Image
from io import BytesIO
import tensorflow as tf
import numpy as np
from object_detection.utils import label_map_util
from object_detection.utils import visualization_utils as viz_utils

detect_fn = None
category_index = None

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

    person_indices = detections['detection_classes'] == 1

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
    PATH_TO_SAVED_MODEL = "../../models/H1/saved_model"
    PATH_TO_LABELS = "../../models/label_map.pbtxt"

    print('Loading model...', end='')
    start_time = time.time()

    detect_fn = tf.saved_model.load(PATH_TO_SAVED_MODEL)

    end_time = time.time()
    elapsed_time = end_time - start_time
    print('Done! Took {} seconds'.format(elapsed_time))

    category_index = label_map_util.create_category_index_from_labelmap(PATH_TO_LABELS,
                                                                    use_display_name=True)

    connection = FTP(config.FTP_HOST, config.FTP_USER, config.FTP_PASS, timeout=60)

    while(True):
        connection = handle_FTP_connection(connection)

        filenames = connection.nlst(config.FTP_DIR)

        for filename in filenames:
            img_data = BytesIO()
            connection.retrbinary("RETR " + filename, img_data.write)
            image = Image.open(img_data)
            
            image, max_score = run_inference(image)
            Image.fromarray(image).show()
            #image.show()
            img_data.close()

        time.sleep(3)
