import os
import numpy as np
import matplotlib.pyplot as plt
import glob
import matplotlib.cm as cm
import matplotlib.animation as animation
import math
import cv2

frames = []
fig = plt.figure()

for i, filename in enumerate(sorted(glob.glob("./motion_masks/*.txt"))):
    # if i != 85:
    #     continue
    file = open(filename, "r")

    line = file.readline()

    img_array = []

    accumelated_x = 0
    accumelated_y = 0
    mean_x = 0
    mean_y = 0
    diff_sum_x = 0
    diff_sum_y = 0
    variance_x = 0
    variance_y = 0
    pixel_counter = 0

    for j, char in enumerate(line.strip()):
        if char == '0':
            pixel_counter += 1
            accumelated_x += j % 32
            accumelated_y += math.floor(j / 32)
        img_array.append(int(char))

    if pixel_counter == 0:
        continue

    mean_x = accumelated_x/pixel_counter
    mean_y = accumelated_y/pixel_counter

    for j, char in enumerate(line.strip()):
        if char == '0':
            diff_sum_x += (j % 32 - mean_x)**2
            diff_sum_y += (math.floor(j / 32) - mean_y)**2
    
    variance_x = diff_sum_x/pixel_counter
    variance_y = diff_sum_y/pixel_counter

    img_1d = np.array(img_array)

    img_2d = np.reshape(img_1d, (24, 32))
    img_2d = np.float32(img_2d)
    rgb_img = cv2.cvtColor(img_2d,cv2.COLOR_GRAY2RGB)

    half_width = max(variance_x, variance_y)*1.1

    start_point = (int(mean_x - half_width), int(mean_y - half_width))
    end_point = (int(mean_x + half_width), int(mean_y + half_width))

    img = cv2.rectangle(np.float32(rgb_img), start_point, end_point, (255,255,0), 1)

    #plt.imshow(img)

    frames.append([plt.imshow(img, cmap=cm.Greys_r,animated=True)])

ani = animation.ArtistAnimation(fig, frames, interval=50, blit=True,
                                repeat_delay=1000)

#ani.save('median_outputs/out_20210216_3.gif')
plt.show()