import os
import random
from PIL import Image
import numpy as np

# --- Настройки ---
OUTPUT_DIR = "test_dataset"
NUM_EACH = 50  # По 50 файлов каждого класса (итого 150)
STOP_MARKER = b"#####"

# --- Примеры текста и кода ---
text_samples = [
    "Привет! Это тестовое сообщение, скрытое в картинке методом LSB.",
    "Стеганография — это искусство скрывать информацию на виду.",
    "The quick brown fox jumps over the lazy dog. Hello world!",
    "Информационная безопасность очень важна в современном мире."
]

code_samples = [
    "#include <iostream>\nint main() { return 0; }\n",
    "def hello():\n    print('Hello, stego!')\n    return True\n",
    "function test() { console.log('JS code'); }\n",
    "public class Main { public static void main(String[] a) {} }"
]

def embed_lsb(img, payload_bytes):
    """Встраивает байты в синий канал (LSB)"""
    img = img.convert("RGB")
    pixels = img.load()
    # Добавляем маркер в конец
    full_payload = payload_bytes + STOP_MARKER
    bits = "".join(format(b, '08b') for b in full_payload)
    
    idx = 0
    w, h = img.size
    for y in range(h):
        for x in range(w):
            if idx < len(bits):
                r, g, b = pixels[x, y]
                # Меняем младший бит синего канала
                b = (b & ~1) | int(bits[idx])
                pixels[x, y] = (r, g, b)
                idx += 1
            else:
                break
        if idx >= len(bits):
            break
    return img

def make_base_image():
    """Создает базовую картинку с шумом/градиентом"""
    # Делаем картинку 128x128, чтобы точно влезло 256 байт (нужно 2048 пикселей, а тут 16384)
    w, h = 128, 128
    # Создаем случайный цветной шум
    img_array = np.random.randint(0, 256, (h, w, 3), dtype=np.uint8)
    return Image.fromarray(img_array)

if not os.path.exists(OUTPUT_DIR):
    os.makedirs(OUTPUT_DIR)

print(f"Генерация файлов в папку '{OUTPUT_DIR}'...")

# 1. Чистые картинки (Empty)
for i in range(NUM_EACH):
    img = make_base_image()
    filename = f"{OUTPUT_DIR}/clean_{i:03d}.png"
    img.save(filename)
    print(f"[{i+1}/{NUM_EACH*3}] Создан {filename} (Пусто)")

# 2. Картинки с текстом (Text)
for i in range(NUM_EACH):
    img = make_base_image()
    text = random.choice(text_samples).encode('utf-8')
    img = embed_lsb(img, text)
    filename = f"{OUTPUT_DIR}/text_{i:03d}.png"
    img.save(filename)
    print(f"[{NUM_EACH+i+1}/{NUM_EACH*3}] Создан {filename} (Текст)")

# 3. Картинки с кодом (Code)
for i in range(NUM_EACH):
    img = make_base_image()
    code = random.choice(code_samples).encode('utf-8')
    img = embed_lsb(img, code)
    filename = f"{OUTPUT_DIR}/code_{i:03d}.png"
    img.save(filename)
    print(f"[{NUM_EACH*2+i+1}/{NUM_EACH*3}] Создан {filename} (Код)")

print("\n✅ Готово! Все 150 файлов созданы.")