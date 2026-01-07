#ifndef SV_TYPES_H
#define SV_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#define MAX_STEPS 128
#define MAX_WRITES_PER_STEP 4
#define MAX_INSTR_LEN 64
#define MAX_LABEL_LEN 32

typedef struct {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
} SV_Regs;

typedef struct {
    uint64_t addr;
    uint64_t value;
    char label[MAX_LABEL_LEN];   // "" if unused
} SV_Write;

typedef struct {
    char instr[MAX_INSTR_LEN];
    SV_Regs regs;
    size_t write_count;
    SV_Write writes[MAX_WRITES_PER_STEP];
} SV_Step;

typedef struct {
    SV_Regs regs;
    bool ok;
    char msg[256];
    size_t step_count;
    SV_Step steps[MAX_STEPS];
} SV_Sim;

#endif
