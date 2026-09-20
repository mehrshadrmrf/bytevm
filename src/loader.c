#include "bytevm/loader.h"

#include <string.h>

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

VmResult vm_load(VM *vm, const uint8_t *data, size_t size)
{
    if (size < BVM_HEADER_SIZE) {
        return VM_ERR_BAD_BYTECODE_HEADER;
    }
    if (memcmp(data, "BVM1", 4) != 0) {
        return VM_ERR_BAD_BYTECODE_HEADER;
    }
    if (data[4] != 1 || data[5] != 0) { /* version must be 1, reserved must be 0 */
        return VM_ERR_BAD_BYTECODE_HEADER;
    }

    /* Widened to uint32_t immediately so every comparison below is
     * unsigned-vs-unsigned of the same width -- avoids any
     * signed/unsigned comparison ambiguity entirely, rather than relying
     * on casts scattered through each check. */
    uint32_t entry_point = read_u16_le(&data[6]);
    uint32_t code_size = read_u16_le(&data[8]);
    uint32_t code_addr = read_u16_le(&data[10]);
    uint32_t data_size = read_u16_le(&data[12]);
    uint32_t data_addr = read_u16_le(&data[14]);

    if (size < (size_t)BVM_HEADER_SIZE + code_size + data_size) {
        return VM_ERR_BAD_BYTECODE_HEADER; /* file shorter than the header claims */
    }

    uint32_t code_end = code_addr + code_size;
    uint32_t data_end = data_addr + data_size;
    if (code_end > 0x10000u || data_end > 0x10000u) {
        return VM_ERR_BAD_BYTECODE_HEADER; /* region overflows the 64KB address space */
    }
    if (!(entry_point >= code_addr && entry_point < code_end)) {
        return VM_ERR_BAD_BYTECODE_HEADER; /* entry_point outside the code blob */
    }
    if (code_addr < data_end && data_addr < code_end) {
        return VM_ERR_BAD_BYTECODE_HEADER; /* code/data regions overlap */
    }

    /* Every check passed -- only now does vm get touched. */
    cpu_init(&vm->cpu);
    mem_init(&vm->memory);
    vm->halted = false;
    vm->last_error = VM_OK;
    vm->error_pc = 0;

    const uint8_t *code_bytes = data + BVM_HEADER_SIZE;
    const uint8_t *data_bytes = code_bytes + code_size;

    uint32_t i;
    for (i = 0; i < code_size; i++) {
        mem_write8(&vm->memory, (uint16_t)(code_addr + i), code_bytes[i]);
    }
    for (i = 0; i < data_size; i++) {
        mem_write8(&vm->memory, (uint16_t)(data_addr + i), data_bytes[i]);
    }

    vm->cpu.pc = (uint16_t)entry_point;

    /* code_end/data_end can legitimately be 0x10000 (a region reaching
     * exactly the top of the address space). Clamp to 0xFFFF rather than
     * truncating straight to uint16_t, which would silently wrap 0x10000
     * to 0 and disable the overflow check entirely (DESIGN.md section 6).
     * Clamping to 0xFFFF is the correct behavior: it makes every
     * PUSH/CALL overflow immediately, which is right, since no address
     * is actually free for the stack when code or data claims the very
     * top of memory. */
    uint32_t limit = (code_end > data_end) ? code_end : data_end;
    vm->stack_limit = (uint16_t)((limit > 0xFFFFu) ? 0xFFFFu : limit);

    return VM_OK;
}
