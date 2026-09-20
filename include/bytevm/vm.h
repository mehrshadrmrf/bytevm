#ifndef BYTEVM_VM_H
#define BYTEVM_VM_H

#include <stdbool.h>
#include <stdint.h>

#include "bytevm/common.h"
#include "bytevm/cpu.h"
#include "bytevm/memory.h"

typedef struct {
    CPU cpu;
    Memory memory;
    bool halted;
    VmResult last_error; /* meaningful when halted && last_error != VM_OK */
    uint16_t error_pc;   /* PC at which last_error occurred */

    /* DESIGN.md section 6: top of the highest-addressed loaded region
     * (code or data). PUSH/CALL halt with VM_ERR_STACK_OVERFLOW rather
     * than letting the stack grow below this. vm_init defaults it to 0
     * (no program loaded, so nothing is reserved); Phase 6's loader is
     * responsible for setting it correctly once a .bvm is loaded. */
    uint16_t stack_limit;
} VM;

/* Resets CPU and memory to their power-on state and clears
 * halted/last_error/error_pc/stack_limit. Does not load a program --
 * that's the loader's job (DESIGN.md section 10, Phase 6). */
void vm_init(VM *vm);

/* Runs one fetch/decode/execute cycle (DESIGN.md section 2.1).
 *
 * If the VM is already halted, this is a defined no-op (DESIGN.md
 * section 9): no fetch, no decode, no execute, PC untouched, and the
 * call returns whatever last_error already holds (VM_OK if the VM
 * halted via HALT/SYS EXIT rather than an error).
 *
 * Otherwise: decodes the instruction at cpu.pc (instruction.h), advances
 * PC past it, then executes it. On any failure (decode or execute), the
 * VM halts, last_error is set to the failing VmResult, and error_pc is
 * set to the address of the instruction that faulted -- not any address
 * PC may have partially advanced to, since a failed decode never
 * commits its PC advance (instruction_decode leaves *pc untouched on
 * error), and a failed execute's PC has already been advanced past the
 * faulting instruction per the fetch step, which points at the *next*
 * instruction, not the one that failed.
 *
 * Returns the VmResult of this step (VM_OK on success). */
VmResult vm_step(VM *vm);

#endif /* BYTEVM_VM_H */
