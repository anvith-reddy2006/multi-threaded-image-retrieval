from PIL import Image, ImageDraw

# Create a brand new 32x32 RGB image (not from CIFAR-10)
img = Image.new('RGB', (32, 32), color = (50, 120, 220)) # Blue background

# Draw a red square in the center
draw = ImageDraw.Draw(img)
draw.rectangle([8, 8, 23, 23], fill=(220, 40, 40))

# Save as custom_query.png
img.save("custom_query.png")
print("Created custom_query.png (A custom 32x32 Blue/Red image from outside the dataset)!")
