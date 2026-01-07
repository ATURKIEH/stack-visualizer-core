# Stack Visualizer Core – Engine Spec

## Goal
Simulate stack frames, stack pointer (SP), frame pointer (FP),
and local variable layout step-by-step for teaching purposes.

No real execution. Deterministic simulation.

---

## Supported Input (Learn Mode)

### Pseudocode (ARM-style)
Example:
PUSH FP
MOV FP, SP
SUB SP, SP, #32
STORE FP-4 = x + 1

makefile
Copy code

### Simplified x86-style
Example:
push rbp
mov rbp, rsp
sub rsp, 0x20
mov [rbp-0x4], eax

yaml
Copy code

---

## Engine API (WASM)

### simulate(input: string, syntax: enum) → JSON

Returns a JSON string describing step-by-step execution.

---

## Trace Output Schema

Each step contains:

- step_index
- instruction (string)
- sp (hex)
- fp (hex)
- stack (array of cells)
- writes (optional memory writes)
- notes (optional explanation)

### Stack Cell
{
address: "0x1000",
offset: "FP-0x4",
value: "0x00000006",
label: "local y"
}

yaml
Copy code

---

## Address Model
- Fake base address (e.g. SP starts at 0x1000)
- Grows downward
- Hexadecimal values only