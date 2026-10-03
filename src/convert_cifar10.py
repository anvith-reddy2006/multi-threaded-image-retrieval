import os
import urllib.request
import tarfile
import pickle
from PIL import Image

URL = "https://www.cs.toronto.edu/~kriz/cifar-10-python.tar.gz"
ARCHIVE = "cifar-10-python.tar.gz"
EXTRACTED_DIR = "cifar-10-batches-py"
OUTPUT_DIR = "dataset/train"
NUM_IMAGES = 5000

if not os.path.exists(ARCHIVE):
    print(f"Downloading CIFAR-10 (162 MB) from {URL}...")
    urllib.request.urlretrieve(URL, ARCHIVE)

if not os.path.exists(EXTRACTED_DIR):
    print("Extracting CIFAR-10...")
    with tarfile.open(ARCHIVE, "r:gz") as tar:
        tar.extractall()

os.makedirs(OUTPUT_DIR, exist_ok=True)

def load_batch(batch_path):
    with open(batch_path, "rb") as f:
        return pickle.load(f, encoding="bytes")

count = 0
batch_path = os.path.join(EXTRACTED_DIR, "data_batch_1")
data = load_batch(batch_path)
images = data[b"data"]
labels = data[b"labels"]

print(f"Converting {NUM_IMAGES} real CIFAR-10 images...")
for i in range(min(NUM_IMAGES, len(images))):
    img_data = images[i].reshape(3, 32, 32).transpose(1, 2, 0)
    img = Image.fromarray(img_data)
    
    filename = os.path.join(OUTPUT_DIR, f"image_{count:04d}_class_{labels[i]}.png")
    img.save(filename)
    count += 1

img_data = images[NUM_IMAGES].reshape(3, 32, 32).transpose(1, 2, 0)
img = Image.fromarray(img_data)
img.save("query.png")
print("Done generating real CIFAR dataset and query image.")
