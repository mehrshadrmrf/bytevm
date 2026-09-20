#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bytevm/loader.h"
#include "test_framework.h"

/* Builds a well-formed .bvm image into `out` and returns its length. */
static size_t build_bvm(uint8_t *out, uint16_t entry_point, uint16_t code_addr, const uint8_t *code,
                        uint16_t code_size, uint16_t data_addr, const uint8_t *data,
                        uint16_t data_size)
{
    size_t i = 0;
    out[i++] = 'B';
    out[i++] = 'V';
    out[i++] = 'M';
    out[i++] = '1';
    out[i++] = 1; /* version */
    out[i++] = 0; /* reserved */
    out[i++] = (uint8_t)(entry_point & 0xFFu);
    out[i++] = (uint8_t)(entry_point >> 8);
    out[i++] = (uint8_t)(code_size & 0xFFu);
    out[i++] = (uint8_t)(code_size >> 8);
    out[i++] = (uint8_t)(code_addr & 0xFFu);
    out[i++] = (uint8_t)(code_addr >> 8);
    out[i++] = (uint8_t)(data_size & 0xFFu);
    out[i++] = (uint8_t)(data_size >> 8);
    out[i++] = (uint8_t)(data_addr & 0xFFu);
    out[i++] = (uint8_t)(data_addr >> 8);
    memcpy(&out[i], code, code_size);
    i += code_size;
    memcpy(&out[i], data, data_size);
    i += data_size;
    return i;
}

int test_loader(void)
{
    int failures = 0;
    VM vm;
    uint8_t buf[64];

    const uint8_t code[] = {0xFF, 0x00}; /* HALT */
    const uint8_t data[] = {0xAB, 0xCD, 0xEF};

    /* ---- Valid load ---- */
    {
        vm_init(&vm);
        size_t len = build_bvm(buf, 0x0000, 0x0000, code, sizeof(code), 0x1000, data, sizeof(data));
        VmResult r = vm_load(&vm, buf, len);

        TEST_ASSERT(r == VM_OK, "a well-formed .bvm loads successfully", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0000, "PC is set to entry_point", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.sp, CPU_INITIAL_SP, "SP is reset to CPU_INITIAL_SP", &failures);
        TEST_ASSERT(!vm.halted, "a freshly loaded VM is not halted", &failures);
        TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, 0x0000), 0xFF, "code byte 0 lands at code_addr",
                           &failures);
        TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, 0x1000), 0xAB, "data byte 0 lands at data_addr",
                           &failures);
        TEST_ASSERT_EQ_U16(mem_read8(&vm.memory, 0x1002), 0xEF,
                           "the last data byte lands at data_addr + data_size - 1", &failures);
        TEST_ASSERT_EQ_U16(vm.stack_limit, 0x1003,
                           "stack_limit == max(code_end, data_end) == data_addr + data_size",
                           &failures);
    }

    /* ---- Rejections (DESIGN.md section 10) -- each must leave vm untouched ---- */
    {
        vm_init(&vm);
        vm.cpu.r[0] = 0xDEAD; /* sentinel to prove a rejected load doesn't mutate vm */

        uint8_t bad_magic[16] = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        VmResult r = vm_load(&vm, bad_magic, sizeof(bad_magic));
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER, "bad magic is rejected", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0xDEAD, "a rejected load leaves vm untouched", &failures);
    }
    {
        vm_init(&vm);
        size_t len = build_bvm(buf, 0, 0, code, sizeof(code), 0x1000, data, sizeof(data));
        buf[4] = 2; /* version 2, unsupported */
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER, "an unsupported version is rejected",
                    &failures);
    }
    {
        vm_init(&vm);
        size_t len = build_bvm(buf, 0, 0, code, sizeof(code), 0x1000, data, sizeof(data));
        buf[5] = 1; /* reserved must be 0 */
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER, "a non-zero reserved byte is rejected",
                    &failures);
    }
    {
        vm_init(&vm);
        size_t len = build_bvm(buf, 0, 0, code, sizeof(code), 0x1000, data, sizeof(data));
        VmResult r = vm_load(&vm, buf, len - 1); /* file truncated by one byte */
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER,
                    "a file shorter than code_size + data_size claims is rejected", &failures);
    }
    {
        vm_init(&vm);
        /* code_addr + code_size overflows the 64KB address space. */
        size_t len = build_bvm(buf, 0xFFFF, 0xFFFF, code, sizeof(code), 0x1000, data, sizeof(data));
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER,
                    "a code region overflowing the address space is rejected", &failures);
    }
    {
        vm_init(&vm);
        /* entry_point (0x0010) is outside [0x0000, 0x0002). */
        size_t len = build_bvm(buf, 0x0010, 0x0000, code, sizeof(code), 0x1000, data, sizeof(data));
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER,
                    "an entry_point outside the code blob is rejected", &failures);
    }
    {
        vm_init(&vm);
        /* code = [0x0000, 0x0002), data placed at 0x0001 -- overlaps. */
        size_t len = build_bvm(buf, 0x0000, 0x0000, code, sizeof(code), 0x0001, data, sizeof(data));
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_ERR_BAD_BYTECODE_HEADER, "overlapping code/data regions are rejected",
                    &failures);
    }

    /* ---- stack_limit clamping at the top of the address space ---- */
    {
        vm_init(&vm);
        /* code occupies the very last 2 bytes of memory: code_end ==
         * 0x10000 exactly. Truncating that straight to uint16_t would
         * wrap to 0 and silently disable overflow checking -- must
         * clamp to 0xFFFF instead (DESIGN.md section 6/10). */
        const uint8_t no_data[1] = {0};
        size_t len = build_bvm(buf, 0xFFFE, 0xFFFE, code, sizeof(code), 0x0000, no_data, 0);
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_OK, "code reaching exactly the top of memory is accepted", &failures);
        TEST_ASSERT_EQ_U16(vm.stack_limit, 0xFFFF,
                           "stack_limit clamps to 0xFFFF instead of wrapping to 0", &failures);
    }

    /* ---- vm_load also implements "reset": reloading the same bytes
     * discards any runtime state changes (DESIGN.md section 13) ---- */
    {
        vm_init(&vm);
        const uint8_t reset_code[] = {
            0x01, 0x12, 0x00, 0x2A, 0x00, /* MOV R0, 42 */
            0xFF, 0x00,                   /* HALT */
        };
        size_t len = build_bvm(buf, 0x0000, 0x0000, reset_code, sizeof(reset_code), 0x1000, data,
                               sizeof(data));
        vm_load(&vm, buf, len);
        while (!vm.halted) {
            vm_step(&vm);
        }
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 42, "the program runs and sets R0", &failures);

        /* Reload the identical bytes: should look exactly like the
         * first load, as if nothing had run. */
        VmResult r = vm_load(&vm, buf, len);
        TEST_ASSERT(r == VM_OK, "reloading for reset succeeds", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.r[0], 0, "reset clears R0 (fresh cpu_init)", &failures);
        TEST_ASSERT_EQ_U16(vm.cpu.pc, 0x0000, "reset sets PC back to entry_point", &failures);
        TEST_ASSERT(!vm.halted, "reset clears halted", &failures);
    }

    return failures;
}
