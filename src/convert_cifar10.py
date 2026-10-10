import os
import urllib.request
import tarfile
import pickle
from PIL import Image

URL = "https://www.cs.toronto.edu/~kriz/cifar-10-python.tar.gz"
ARCHIVE = "cifar-10-python.tar.gz"
EXTRACTED_DIR = "cifar-10-batches-py"
OUTPUT_DIR = "dataset/train"
NUM_IMAGES = 10000

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
images, labels = [], []
# each CIFAR-10 batch holds 10000 images; load batches until we have
# NUM_IMAGES + 1 (the extra one becomes query.png, which is NOT in the database)
for b in range(1, 6):
    data = load_batch(os.path.join(EXTRACTED_DIR, f"data_batch_{b}"))
    images.extend(data[b"data"])
    labels.extend(data[b"labels"])
    if len(images) > NUM_IMAGES:
        break

print(f"Converting {NUM_IMAGES} real CIFAR-10 images...")
for i in range(NUM_IMAGES):
    img_data = images[i].reshape(3, 32, 32).transpose(1, 2, 0)
    img = Image.fromarray(img_data)
    
    filename = os.path.join(OUTPUT_DIR, f"image_{count:04d}_class_{labels[i]}.png")
    img.save(filename)
    count += 1

img_data = images[NUM_IMAGES].reshape(3, 32, 32).transpose(1, 2, 0)
img = Image.fromarray(img_data)
img.save("query.png")
print("Done generating real CIFAR dataset and query image.")
