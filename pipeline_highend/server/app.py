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
import secrets

detect_fn = None
category_index = None
session = None

def load_image_into_numpy_array(path):
    """Load an image from file into a numpy array.

    Puts image into numpy array to feed into tensorflow graph.
    Note that by convention we put it into a numpy array with shape
    (height, width, channels), where channels=3 for RGB.

    Args:
      path: the file path to the image

    Returns:
      uint8 numpy array with shape (img_height, img_width, 3)
    """
    return np.array(Image.open(path))

def run_inference(message):
    image_path = "../../dataset/test/image_{0:04}.jpg".format(random.randint(0, 101))
    #print('Running inference for {}... '.format(image_path), end='')

    file_jpgdata = BytesIO(message)
    img = Image.open(file_jpgdata)
    image_np = np.array(img)

    #image_np = load_image_into_numpy_array(image_path)

    # Things to try:
    # Flip horizontally
    # image_np = np.fliplr(image_np).copy()

    # Convert image to grayscale
    # image_np = np.tile(
    #     np.mean(image_np, 2, keepdims=True), (1, 1, 3)).astype(np.uint8)

    # The input needs to be a tensor, convert it using `tf.convert_to_tensor`.
    input_tensor = tf.convert_to_tensor(image_np)
    # The model expects a batch of images, so add an axis with `tf.newaxis`.
    input_tensor = input_tensor[tf.newaxis, ...]

    # input_tensor = np.expand_dims(image_np, 0)
    detections = detect_fn(input_tensor)

    max_score = np.max(detections['detection_scores'])

    # All outputs are batches tensors.
    # Convert to numpy arrays, and take index [0] to remove the batch dimension.
    # We're only interested in the first num_detections.
    num_detections = int(detections.pop('num_detections'))

    detections = {key: value[0, :num_detections].numpy()
                    for key, value in detections.items()}
    detections['num_detections'] = num_detections

    # detection_classes should be ints.
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
            min_score_thresh=.30,
            agnostic_mode=False)

    image = image_np_with_detections
    #buf = BytesIO()
    #image.save(buf, format="JPEG")

    return image, max_score

async def hello(websocket, path):
    while True:
        async for message in websocket:
            image, max_score = run_inference(message)
            cv2.imshow('image', image)
            cv2.waitKey(1)
            
            print("Max score: ", max_score)
            if max_score > 0.3:
                print("*********HUMAN DETECTED**********")
                img = Image.fromarray(image)
                temp = BytesIO()
                img.save(temp, format="JPEG")
                temp.seek(0)
                session.storbinary('STOR /thesis/highend/test.jpeg', temp)

                await websocket.send("humandetected")

if __name__ == '__main__':
    PATH_TO_SAVED_MODEL = "../local_inference/model/saved_model"
    PATH_TO_LABELS = "../local_inference/label_map.pbtxt"

    print('Loading model...', end='')
    start_time = time.time()

    # Load saved model and build the detection function
    detect_fn = tf.saved_model.load(PATH_TO_SAVED_MODEL)

    end_time = time.time()
    elapsed_time = end_time - start_time
    print('Done! Took {} seconds'.format(elapsed_time))

    category_index = label_map_util.create_category_index_from_labelmap(PATH_TO_LABELS,
                                                                    use_display_name=True)

    session = ftplib.FTP(secrets.FTP_HOST, secrets.FTP_USER, secrets.FTP_PASS)
    session.set_debuglevel(2)

    asyncio.get_event_loop().run_until_complete(
        websockets.serve(hello, '192.168.243.93', 8888, ping_interval=None))
    asyncio.get_event_loop().run_forever()