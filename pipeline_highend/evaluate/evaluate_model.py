import time
from object_detection.utils import label_map_util
from object_detection.utils import visualization_utils as viz_utils
import tensorflow as tf
import numpy as np
from PIL import Image
import warnings
import glob
import cv2
warnings.filterwarnings('ignore')   # Suppress Matplotlib warnings

PATH_TO_SAVED_MODEL = "../models/H2/saved_model"
PATH_TO_LABELS = "../models/label_map.pbtxt"

tp = 0
tn = 0
fp = 0
fn = 0
ground_truth = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

print('Loading model...', end='')
start_time = time.time()

detect_fn = tf.saved_model.load(PATH_TO_SAVED_MODEL)

end_time = time.time()
elapsed_time = end_time - start_time
print('Done! Took {} seconds'.format(elapsed_time))

category_index = label_map_util.create_category_index_from_labelmap(PATH_TO_LABELS,
                                                                    use_display_name=True)

def load_image_into_numpy_array(path):
    return np.array(Image.open(path))

def run_inference(image):
    input_tensor = tf.convert_to_tensor(image)
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

if __name__ == '__main__':
  for i, image_path in enumerate(sorted(glob.glob('/Users/pontusarnesson/Documents/Skola/femman/exjobb/exjobb/poacher-detector/dataset/test/*.jpg'))):
    image_np = load_image_into_numpy_array(image_path)

    image, max_score = run_inference(image_np)

    if max_score > 0.5:
      if ground_truth[i] == 1:
        tp+=1
      else:
        fp+=1
    else:
      if ground_truth[i] == 1:
        fn+=1
      else:
        tn+=1

    img = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    cv2.imshow('image', img)
    cv2.waitKey(1)

  print("True Positives: ", tp)
  print("True Negatives: ", tn)
  print("False Positives: ", fp)
  print("False Negatives: ", fn)