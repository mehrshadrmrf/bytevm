#include "bytevm/memory.h"

#include <string.h>

void mem_init(Memory *mem)
{
    memset(mem->data, 0, sizeof(mem->data));
}

uint8_t mem_read8(const Memory *mem, uint16_t addr)
{
    return mem->data[addr];
}

void mem_write8(Memory *mem, uint16_t addr, uint8_t value)
{
    mem->data[addr] = value;
}

uint16_t mem_read16(const Memory *mem, uint16_t addr)
{
    return (uint16_t)(mem->data[addr] | (uint16_t)(mem->data[(uint16_t)(addr + 1)] << 8));
}

void mem_write16(Memory *mem, uint16_t addr, uint16_t value)
{
    mem->data[addr] = (uint8_t)(value & 0xFFu);
    mem->data[(uint16_t)(addr + 1)] = (uint8_t)(value >> 8);
}
