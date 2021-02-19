import os
import numpy as np
import matplotlib.pyplot as plt
import glob
import matplotlib.cm as cm
import matplotlib.animation as animation

frames = []
fig = plt.figure()

for filename in sorted(glob.glob("./median_filter/motion_masks/*.txt")):
    file = open(filename, "r")

    line = file.readline()

    img_array = []

    for char in line.strip():
        img_array.append(int(char))

    img_1d = np.array(img_array)

    img_2d = np.reshape(img_1d, (24, 32))

    frames.append([plt.imshow(img_2d, cmap=cm.Greys_r,animated=True)])

ani = animation.ArtistAnimation(fig, frames, interval=50, blit=True,
                                repeat_delay=1000)

#ani.save('median_filter/median_outputs/out_20210216_3.gif')
plt.show()