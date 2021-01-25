import os
import argparse
from shutil import copyfile

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Move images from sub directories of root to target directory')
    parser.add_argument('-r', '--root', help='Root of the folder structure you want to process', required=True)
    parser.add_argument('-t', '--target', help='Target folder', required=True)
    args = parser.parse_args()

    counter = 0
    for root, dirs, files in os.walk(args.root):
        for name in files:
            if name.endswith((".jpg", ".jpeg")):
                copyfile(root + "/" + name, args.target + '/image_{0:04}'.format(counter) + ".jpg")
                counter += 1
    print("Copied " + str(counter) + " files successfully!")