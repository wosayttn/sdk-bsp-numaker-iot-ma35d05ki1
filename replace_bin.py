import os
import sys

def replace_in_file(path, old_str, new_str):
    try:
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
    except UnicodeDecodeError:
        return False
    
    if old_str in content:
        content = content.replace(old_str, new_str)
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(content)
        print(f"Updated {path}")
        return True
    return False

def main():
    directory = r"z:\rtthread\bsp\nuvoton\numaker-iot-ma35d05ki1"
    old_str = "MA35D05KI67C"
    new_str = "MA35D05KI67C"
    
    count = 0
    for root, dirs, files in os.walk(directory):
        if '.git' in root or '.antigravityignore' in root:
            continue
        for file in files:
            path = os.path.join(root, file)
            if replace_in_file(path, old_str, new_str):
                count += 1
                
    print(f"Total files updated: {count}")

if __name__ == "__main__":
    main()
