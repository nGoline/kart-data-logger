import os
import re

try:
    Import("env")
    project_dir = env.get("PROJECT_DIR")
except NameError:
    project_dir = os.getcwd()

print("--- Running LVGL Stride Patcher (Width * 2) ---", flush=True)

patch_count = 0

for root, dirs, files in os.walk(project_dir):
    if ".pio" in root: 
        continue 
    
    for file in files:
        if file.startswith("ui_img_") and file.endswith(".c"):
            filepath = os.path.join(root, file)
            
            with open(filepath, 'r') as f:
                content = f.read()
                
            if '.header.stride' in content:
                continue 
                
            # Find the width value
            w_match = re.search(r'\.header\.w\s*=\s*(\d+)', content)
            
            if w_match:
                width = w_match.group(1)
                
                # Inject stride right after height
                content = re.sub(
                    r'(\.header\.h\s*=\s*\d+,)', 
                    r'\1\n    .header.stride = ' + width + ' * 2,', 
                    content
                )
                
                with open(filepath, 'w') as f:
                    f.write(content)
                    
                print(f" -> Patched: {file} (Stride: {int(width)*2})", flush=True)
                patch_count += 1

if patch_count > 0:
    print(f"--- Fixed {patch_count} images! ---", flush=True)
else:
    print("--- All images already patched. ---", flush=True)