import pickle
from pathlib import Path
from PIL import Image

INPUT_DIR = Path("cifar-10-batches-py")
OUTPUT_DIR = Path("dataset/train")

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

NUM_IMAGES = 10000


def load_batch(batch_path):
    with open(batch_path, "rb") as f:
        return pickle.load(f, encoding="bytes")


count = 0

for batch_num in range(1, 6):
    batch_path = INPUT_DIR / f"data_batch_{batch_num}"
    data = load_batch(batch_path)

    images = data[b"data"]
    labels = data[b"labels"]

    for i in range(len(images)):
        if count >= NUM_IMAGES:
            break

        # CIFAR-10 format:
        # first 1024 = Red
        # next 1024 = Green
        # next 1024 = Blue
        image_data = images[i].reshape(3, 32, 32)
        image_data = image_data.transpose(1, 2, 0)

        image = Image.fromarray(image_data)

        filename = OUTPUT_DIR / f"image_{count:04d}_class_{labels[i]}.png"
        image.save(filename)

        count += 1

    if count >= NUM_IMAGES:
        break

print(f"Converted {count} images.")
