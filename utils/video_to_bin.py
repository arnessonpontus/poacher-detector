import os
import cv2
from PIL import Image
import time

gray = True

def crop(image):
    image  = Image.fromarray(image)
    width  = image.size[0]
    height = image.size[1]

    aspect = width / float(height)

    ideal_width = 320 if gray else 800
    ideal_height = 240 if gray else 600 

    ideal_aspect = ideal_width / float(ideal_height)

    if aspect > ideal_aspect:
        # Then crop the left and right edges:
        new_width = int(ideal_aspect * height)
        offset = (width - new_width) / 2
        resize = (offset, 0, width - offset, height)
    else:
        # ... crop the top and bottom:
        new_height = int(width / ideal_aspect)
        offset = (height - new_height) / 2
        resize = (0, offset, width, height - offset)

    cropped = image.crop(resize).resize((ideal_width, ideal_height), Image.ANTIALIAS)
    return cropped

cap = cv2.VideoCapture('/Users/pontusarnesson/Documents/Skola/femman/exjobb/exjobb/trimmed_angelas_videos/vid20.mov')

recorded_fps = 30
frame_rate = 5
prev = 0
filename_counter = 0

counter = 0
while(True):
    res, frame = cap.read()

    if counter % (recorded_fps/frame_rate) == 0:

        new_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY) if gray else cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        cropped_img = crop(new_frame)

        filename = 'output_images/gray/{0:04}'.format(filename_counter) if gray else 'output_images/color/{0:04}'.format(filename_counter)

        cropped_img.save(filename + ".jpg")

        if gray:
            f = open(filename + ".bin", "wb")
            f.write(cropped_img.tobytes())

        filename_counter+=1

        cv2.imshow('frame', new_frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    counter += 1

# When everything done, release the capture
cap.release()
cv2.destroyAllWindows()