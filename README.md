# 🧠 Assembly Stack Visualizer

![MIT License](https://img.shields.io/badge/License-MIT-green.svg)
![ARM64](https://img.shields.io/badge/ARM64-supported-blue.svg)
![x86_64](https://img.shields.io/badge/x86__64-supported-blue.svg)
![Web](https://img.shields.io/badge/Web-WASM-purple.svg)
![Status](https://img.shields.io/badge/Status-Active-success.svg)
![C](https://img.shields.io/badge/C-A8B9CC?style=flat&logo=c&logoColor=black)

An interactive, browser-based tool to **visualize stack behavior in assembly programs**.
Designed for students and low-level programmers learning how stack frames, registers,
and memory evolve during execution.

Supports **ARM64** and **x86_64** assembly with step-by-step execution and live stack/register views.

---

## 🚀 Live Demo
👉 https://aturkieh.github.io/stack-visualizer-core/

---

## ✨ Features

- Step-by-step execution of assembly code
- Visual stack frame layout
- Frame Pointer (FP / RBP) and Stack Pointer (SP / RSP) indicators
- Register change highlighting
- Overwrite detection (same stack slot, different register)
- ARM64 and x86_64 support
- Clean, educational UI

---

## 🧠 Supported Architectures

### ARM64
- `mov`
- `str`
- `ldr`
- `stp`
- `ldp`
- `add`
- `sub`
- `ret`

### x86_64
- `push`
- `pop`
- `mov`
- `sub`
- `add`
- `leave`
- `ret`

*(Instruction set intentionally limited for clarity & correctness.)*

---

## 🛠️ Project Structure

stack-visualizer-core/
│
├── engine/ # C simulation engine
│ ├── src/
│ │ └── sv_engine.c
│ ├── include/
│ │ ├── sv_engine.h
│ │ └── sv_types.h
│ └── build/
│
├── web/public/ # Frontend (GitHub Pages)
│ ├── index.html # Landing page
│ ├── visualizer.html # Stack visualizer
│ ├── app.js # UI logic
│ ├── sv_engine.js # WASM bindings
│ ├── sv_engine.wasm
│ └── assets/
│ └── logo.png
│
├── examples/ # Example assembly programs
├── tests/ # C-level tests
├── README.md
└── Makefile

yaml
Copy code

---

## ⚙️ Build (Engine)

```bash
make
```
This compiles the C engine and generates the WebAssembly module.

📦 Deployment
Frontend is deployed using GitHub Pages from:

swift
Copy code
/web/public
👤 Author
Aref Turkieh

GitHub: https://github.com/ATURKIEH

LinkedIn: https://www.linkedin.com/in/aref-turkieh/
