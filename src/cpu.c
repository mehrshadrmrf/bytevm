#include "bytevm/cpu.h"

#include <string.h>

void cpu_init(CPU *cpu)
{
    memset(cpu->r, 0, sizeof(cpu->r));
    cpu->pc = 0;
    cpu->sp = CPU_INITIAL_SP;
    cpu->flags = 0;
}

bool cpu_get_flag(const CPU *cpu, CpuFlagBit bit)
{
    return (bool)((cpu->flags >> bit) & 0x1u);
}

void cpu_set_flag(CPU *cpu, CpuFlagBit bit, bool value)
{
    uint16_t mask = (uint16_t)(1u << bit);
    if (value) {
        cpu->flags = (uint16_t)(cpu->flags | mask);
    } else {
        cpu->flags = (uint16_t)(cpu->flags & (uint16_t)~mask);
    }
}
