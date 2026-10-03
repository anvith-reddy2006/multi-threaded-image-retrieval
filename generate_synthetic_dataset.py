import os
import random
from PIL import Image

output_dir = "dataset/train"
os.makedirs(output_dir, exist_ok=True)

# Generate 1000 synthetic 32x32 PNGs to keep it quick
num_images = 1000
print(f"Generating {num_images} synthetic 32x32 PNGs...")

for i in range(num_images):
    # Random pixels
    img = Image.new('L', (32, 32)) # 'L' = 8-bit pixels, black and white
    pixels = img.load()
    for x in range(32):
        for y in range(32):
            pixels[x, y] = random.randint(0, 255)
            
    img.save(os.path.join(output_dir, f"synthetic_{i:04d}.png"))

# Generate a query image
img = Image.new('L', (32, 32))
pixels = img.load()
for x in range(32):
    for y in range(32):
        pixels[x, y] = random.randint(0, 255)
img.save("query.png")

print("Done generating synthetic dataset and query image.")
