import torch
import torch.nn as nn
import torch.optim as optim
import numpy as np
import math
import random

def get_features(data_bytes):
    if len(data_bytes) == 0: return [0.0]*6
    entropy = 0.0
    counts = {}
    for b in data_bytes: counts[b] = counts.get(b, 0) + 1
    for count in counts.values():
        p = count / len(data_bytes)
        entropy -= p * math.log2(p)
    
    printable = sum(1 for b in data_bytes if 32 <= b <= 126)
    spaces = sum(1 for b in data_bytes if b == 32)
    newlines = sum(1 for b in data_bytes if b == 10 or b == 13)
    code_chars = sum(1 for b in data_bytes if b in b"{}()[];=#<>")
    
    L = len(data_bytes)
    return [entropy/8.0, printable/L, spaces/L, newlines/L, code_chars/L, 0.0]

X, y = [], []

# ТЕКСТЫ (Английский и Русский)
text_samples = [
    b"This is a secret text message. It has many spaces and normal letters. " * 10,
    "Привет! Это секретное сообщение на русском языке. Оно содержит много букв и пробелов. " .encode('utf-8') * 10,
    "Съешь же ещё этих мягких французских булок, да выпей чаю. " .encode('utf-8') * 10
]

# КОД (Python и C++)
code_samples = [
    b"#include <iostream>\nint main() { int x = 0; return 0; }\ndef foo(): return True\n" * 10,
    b"function test() { console.log('hello'); return true; }" * 10
]

for _ in range(10000): # Увеличили датасет для точности
    if random.random() < 0.5: # Текст
        text = random.choice(text_samples)
        length = random.randint(20, 300)
        start = random.randint(0, max(0, len(text)-length))
        data = np.frombuffer(text[start:start+length], dtype=np.uint8)
        y.append(0) # 0 = Текст
    else: # Код
        code = random.choice(code_samples)
        length = random.randint(20, 300)
        start = random.randint(0, max(0, len(code)-length))
        data = np.frombuffer(code[start:start+length], dtype=np.uint8)
        y.append(1) # 1 = Код
    
    X.append(get_features(data))

X = torch.tensor(np.array(X), dtype=torch.float32)
y = torch.tensor(y, dtype=torch.long)

class StegoAI(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1 = nn.Linear(6, 16)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(16, 2) # 0=Текст, 1=Код
    def forward(self, x):
        return self.fc2(self.relu(self.fc1(x)))

model = StegoAI()
criterion = nn.CrossEntropyLoss()
optimizer = optim.Adam(model.parameters(), lr=0.01)

print("Обучение (Текст/Код)...")
for epoch in range(200):
    optimizer.zero_grad()
    loss = criterion(model(X), y)
    loss.backward()
    optimizer.step()

print(f"Ошибка: {loss.item():.4f}")
with open("model_weights.txt", "w") as f:
    for name, param in model.named_parameters():
        f.write(f"{name} " + " ".join(map(str, param.detach().numpy().flatten())) + "\n")
print("Готово!")