import os
import numpy as np
import matplotlib.pyplot as plt

file = open("gray2.txt", "r")

lines = file.readlines()

img_array = []

for line in lines:
    img_array.append(int(line.strip()))

img_1d = np.array(img_array)

img_2d = np.reshape(img_1d, (480, 640))

plt.imshow(img_2d, cmap="gray")
plt.show()