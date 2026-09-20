#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bytevm/loader.h"
#include "bytevm/vm.h"
#include "test_framework.h"

/* DESIGN.md section 14: a fixed step budget per fuzz iteration.
 * Exceeding it counts as a pass, not a failure -- termination isn't the
 * goal, memory safety and determinism are (section 1.2's "guaranteed
 * termination" non-goal). */
#define FUZZ_STEP_BUDGET 5000

static void run_fuzzed(const uint8_t *buf, size_t len)
{
    VM vm;
    VmResult r;
    long steps;

    vm_init(&vm);
    r = vm_load(&vm, buf, len);
    if (r != VM_OK) {
        return; /* rejection is a perfectly fine outcome */
    }

    steps = 0;
    while (!vm.halted && steps < FUZZ_STEP_BUDGET) {
        vm_step(&vm);
        steps++;
    }
    /* Reaching here at all -- halted normally, halted on an error, or
     * hit the step budget -- is what's being tested: the VM must never
     * crash the host process, only ever return a VmResult or run to
     * HALT. There is nothing further to assert; a real memory-safety bug
     * would crash this test binary outright rather than fail an
     * assertion inside it. */
}

int test_fuzz(void)
{
    int failures = 0;
    unsigned seed = 0xC0FFEEu; /* fixed: deterministic, reproducible runs */
    int i;
    size_t j;

    /* Deterministic regression for the exact failure mode named in
     * DESIGN.md section 0/section 14: a program that loops forever
     * (JMP to itself) must not hang the harness -- the step budget must
     * actually bound execution rather than this test looping until
     * someone kills the process. */
    {
        const uint8_t code[] = {0x12, 0x30, 0x00, 0x00}; /* JMP 0x0000 */
        uint8_t buf[BVM_HEADER_SIZE + sizeof(code)];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "BVM1", 4);
        buf[4] = 1;                     /* version */
        buf[5] = 0;                     /* reserved */
        buf[8] = (uint8_t)sizeof(code); /* code_size */
        memcpy(buf + BVM_HEADER_SIZE, code, sizeof(code));

        VM vm;
        VmResult r;
        long steps;

        vm_init(&vm);
        r = vm_load(&vm, buf, sizeof(buf));
        TEST_ASSERT(r == VM_OK, "an infinite JMP-loop program loads", &failures);

        steps = 0;
        while (!vm.halted && steps < FUZZ_STEP_BUDGET) {
            vm_step(&vm);
            steps++;
        }
        TEST_ASSERT(!vm.halted, "an infinite JMP loop never halts on its own", &failures);
        TEST_ASSERT(steps == FUZZ_STEP_BUDGET, "the step budget actually bounds execution",
                    &failures);
    }

    srand(seed);

    /* Pass 1: pure random bytes of random length. Almost always rejected
     * at the magic-byte check within microseconds -- cheap insurance
     * that malformed headers of every shape are handled without
     * crashing, regardless of what garbage precedes a valid-looking one. */
    for (i = 0; i < 2000; i++) {
        uint8_t buf[256];
        size_t len = 1 + (size_t)(rand() % (int)sizeof(buf));
        for (j = 0; j < len; j++) {
            buf[j] = (uint8_t)rand();
        }
        run_fuzzed(buf, len);
    }

    /* Pass 2: a structurally valid header (real magic/version/reserved)
     * with random field values and random code/data bytes. This is the
     * pass that actually reaches instruction_decode/instruction_execute
     * with arbitrary bytecode once a random header happens to satisfy
     * the bounds/entry-point/overlap checks -- the case section 14 means
     * by "runs random instruction sequences". */
    for (i = 0; i < 500; i++) {
        uint16_t entry_point = (uint16_t)rand();
        uint16_t code_size = (uint16_t)rand();
        uint16_t code_addr = (uint16_t)rand();
        uint16_t data_size = (uint16_t)rand();
        uint16_t data_addr = (uint16_t)rand();

        size_t total = (size_t)BVM_HEADER_SIZE + code_size + data_size;
        uint8_t *buf = malloc(total);
        if (buf == NULL) {
            continue;
        }
        memcpy(buf, "BVM1", 4);
        buf[4] = 1;
        buf[5] = 0;
        buf[6] = (uint8_t)(entry_point & 0xFFu);
        buf[7] = (uint8_t)(entry_point >> 8);
        buf[8] = (uint8_t)(code_size & 0xFFu);
        buf[9] = (uint8_t)(code_size >> 8);
        buf[10] = (uint8_t)(code_addr & 0xFFu);
        buf[11] = (uint8_t)(code_addr >> 8);
        buf[12] = (uint8_t)(data_size & 0xFFu);
        buf[13] = (uint8_t)(data_size >> 8);
        buf[14] = (uint8_t)(data_addr & 0xFFu);
        buf[15] = (uint8_t)(data_addr >> 8);
        for (j = BVM_HEADER_SIZE; j < total; j++) {
            buf[j] = (uint8_t)rand();
        }

        run_fuzzed(buf, total);
        free(buf);
    }

    return failures;
}
