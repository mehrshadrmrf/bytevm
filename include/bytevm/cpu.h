#ifndef BYTEVM_CPU_H
#define BYTEVM_CPU_H

#include <stdbool.h>
#include <stdint.h>

/* Value SP is reset to (DESIGN.md section 6). Deliberately 0xFFFE, not
 * 0xFFFF -- see the design doc's revision notes (section 0) for the
 * memory-corruption bug that value avoids. Also doubles as the
 * "stack is empty" sentinel checked by POP/RET underflow detection. */
#define CPU_INITIAL_SP 0xFFFEu

/* FLAGS bit positions (DESIGN.md section 5). Only bits 0-3 are defined;
 * bits 4-15 are reserved and must read as 0. */
typedef enum {
    CPU_FLAG_Z = 0,
    CPU_FLAG_C = 1,
    CPU_FLAG_S = 2,
    CPU_FLAG_O = 3,
} CpuFlagBit;

typedef struct {
    uint16_t r[8];
    uint16_t pc;
    uint16_t sp;
    uint16_t flags;
} CPU;

/* Zeroes R0-R7, PC, and FLAGS; sets SP to CPU_INITIAL_SP. */
void cpu_init(CPU *cpu);

/* Plain bit-level accessors for a single FLAGS bit. These do not compute
 * a flag from an operation's result -- that's the job of the
 * per-instruction-family flag update routines added in Phase 3
 * (DESIGN.md sections 5, 8.2, 8.3), which build on top of these. */
bool cpu_get_flag(const CPU *cpu, CpuFlagBit bit);
void cpu_set_flag(CPU *cpu, CpuFlagBit bit, bool value);

#endif /* BYTEVM_CPU_H */
