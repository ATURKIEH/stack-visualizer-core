# Stack Visualizer Core — Notes

## Target Architectures
- x86-64 (System V ABI) — primary
- ARM64 (AAPCS64) — later

## Registers (x86-64)
- RSP — stack pointer
- RBP — frame pointer
- RIP — instruction pointer

## Canonical Stack Frame (high → low)
arguments
return address
saved frame pointer
local variables
padding / alignment

## Compiler flags used
- -g
- -O0
- -fno-omit-frame-pointer
- -fno-inline

## Observations log
- (empty by design)
