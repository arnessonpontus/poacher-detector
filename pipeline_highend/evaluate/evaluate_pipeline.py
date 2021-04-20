import matplotlib.pyplot as plt
import numpy as np
from PIL import Image
from io import BytesIO
import time
from object_detection.utils import label_map_util
from object_detection.utils import visualization_utils as viz_utils
import tensorflow as tf
import numpy as np
import warnings
warnings.filterwarnings('ignore')   # Suppress Matplotlib warnings
import random
import logging
logger = logging.getLogger('websockets.server')
logger.setLevel(logging.ERROR)
logger.addHandler(logging.StreamHandler())
import cv2
import ftplib
import sys
sys.path.append('/Users/pontusarnesson/Documents/Skola/femman/exjobb/exjobb/poacher-detector/pipeline_highend')
import config
import glob

detection_counter = 0
last_detection_frame = 0
last_event_frame = -999999

NUM_SEQUENCES = 21
tp = 0

def run_inference(image):
    input_tensor = tf.convert_to_tensor(image)
    input_tensor = input_tensor[tf.newaxis, ...]

    detections = detect_fn(input_tensor)

    max_score = np.max(detections['detection_scores'])

    num_detections = int(detections.pop('num_detections'))

    detections = {key: value[0, :num_detections].numpy()
                    for key, value in detections.items()}
    detections['num_detections'] = num_detections

    detections['detection_classes'] = detections['detection_classes'].astype(np.int64)

    image_np_with_detections = image.copy()

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

def load_image_into_numpy_array(path):
    return np.array(Image.open(path))

PATH_TO_SAVED_MODEL = "../models/H1/saved_model"
PATH_TO_LABELS = "../models/label_map.pbtxt"

print('Loading model...', end='')
start_time = time.time()

detect_fn = tf.saved_model.load(PATH_TO_SAVED_MODEL)

end_time = time.time()
elapsed_time = end_time - start_time
print('Done! Took {} seconds'.format(elapsed_time))

category_index = label_map_util.create_category_index_from_labelmap(PATH_TO_LABELS,
                                                                use_display_name=True)

#session = ftplib.FTP(config.FTP_HOST, config.FTP_USER, config.FTP_PASS)
#session.set_debuglevel(0)

for i in range(NUM_SEQUENCES):
    event_created = False
    print("----- STARTING SEQUENCE " + str(i) + " -----")
    
    for j, image_path in enumerate(sorted(glob.glob('sequences/seq_{0:04}/color/*.jpg'.format(i)))):
        if j % 5 != 0:
            continue

        image_np = load_image_into_numpy_array(image_path)

        image, max_score = run_inference(image_np)
        img = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        cv2.imshow('image', img)
        cv2.waitKey(1)

        print("Max score: ", max_score)

        if max_score > 0.5:
            print("*********HUMAN DETECTED**********")

            if j - last_detection_frame > 10:
                detection_counter = 0
            detection_counter += 1
    
            if detection_counter == 2 and j - last_event_frame > 120:
                #img = Image.fromarray(image)
                #temp = BytesIO()
                #img.save(temp, format="JPEG")
                #temp.seek(0)
                #session.storbinary('STOR /thesis-highend/image_' + str(j) + '.jpeg', temp)

                last_event_frame = j

                print("Event was created")
                #print("SENT TO FTP AS image_" + str(j) + '.jpeg')

                event_created = True

        last_detection_frame = j

    if event_created:
        print("event_created = true")
        tp += 1

    last_event_frame = -9999999
    last_detection_frame = 0
    detection_counter = 0

print("True Positives: ", tp)