import asyncio
import websockets
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image
from io import BytesIO
import time
from object_detection.utils import label_map_util
from object_detection.utils import visualization_utils as viz_utils
import tensorflow as tf
import numpy as np
from PIL import Image
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

detect_fn = None
category_index = None
session = None

def run_inference(message):
    file_jpgdata = BytesIO(message)
    img = Image.open(file_jpgdata)
    image_np = np.array(img)

    input_tensor = tf.convert_to_tensor(image_np)
    input_tensor = input_tensor[tf.newaxis, ...]

    detections = detect_fn(input_tensor)

    max_score = np.max(detections['detection_scores'])

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
    #buf = BytesIO()
    #image.save(buf, format="JPEG")

    return image, max_score

async def hello(websocket, path):
    while True:
        async for message in websocket:
            image, max_score = run_inference(message)
            img = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            cv2.imshow('image', img)
            cv2.waitKey(1)
            
            print("Max score: ", max_score)
            if max_score > 0.5:
                print("*********HUMAN DETECTED**********")
                img = Image.fromarray(image)
                temp = BytesIO()
                img.save(temp, format="JPEG")
                temp.seek(0)
                session.storbinary('STOR /thesis-highend/test.jpeg', temp)

                await websocket.send("humandetected")

if __name__ == '__main__':
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

    session = ftplib.FTP(config.FTP_HOST, config.FTP_USER, config.FTP_PASS)
    session.set_debuglevel(0)

    asyncio.get_event_loop().run_until_complete(
        websockets.serve(hello, '192.168.124.93', 8887, ping_interval=None))
    asyncio.get_event_loop().run_forever()