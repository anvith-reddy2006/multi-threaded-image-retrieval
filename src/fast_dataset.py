import os
import random
from PIL import Image

OUTPUT_DIR = "dataset/train"
NUM_IMAGES = 5000

os.makedirs(OUTPUT_DIR, exist_ok=True)

print(f"Generating {NUM_IMAGES} fast images...")

# Create a simple base image (a color gradient)
base_img = Image.new('RGB', (32, 32))
pixels = base_img.load()
for i in range(32):
    for j in range(32):
        pixels[i, j] = (i * 8, j * 8, 150)

# Generate 5000 slightly different variations locally
for count in range(NUM_IMAGES):
    img = base_img.copy()
    px = img.load()
    # Add a tiny dot of noise so they aren't completely identical
    px[random.randint(0, 31), random.randint(0, 31)] = (255, 255, 255)
    
    filename = os.path.join(OUTPUT_DIR, f"image_{count:04d}.png")
    img.save(filename)

# Create the query image
query = base_img.copy()
px = query.load()
px[15, 15] = (0, 0, 0)
query.save("query.png")

print("Done! Fast dataset and query.png created instantly.")
