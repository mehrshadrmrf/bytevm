#ifndef BYTEVM_MEMORY_H
#define BYTEVM_MEMORY_H

#include <stdint.h>

/* 64 KB, flat, byte-addressed (DESIGN.md section 3). */
#define MEMORY_SIZE 65536u

typedef struct {
    uint8_t data[MEMORY_SIZE];
} Memory;

/* Zeroes the entire 64 KB address space. */
void mem_init(Memory *mem);

uint8_t mem_read8(const Memory *mem, uint16_t addr);
void mem_write8(Memory *mem, uint16_t addr, uint8_t value);

/* Little-endian 16-bit access (low byte at the lower address). Address
 * arithmetic wraps modulo 65536 rather than being undefined behavior
 * (DESIGN.md section 3): reading/writing at addr == 0xFFFF touches
 * 0xFFFF and 0x0000. This is intentional and exercised by test_memory.c;
 * see DESIGN.md section 6 for why the stack's initial SP value is chosen
 * specifically so normal stack use never reaches this wraparound. */
uint16_t mem_read16(const Memory *mem, uint16_t addr);
void mem_write16(Memory *mem, uint16_t addr, uint16_t value);

#endif /* BYTEVM_MEMORY_H */
