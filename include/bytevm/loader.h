#ifndef BYTEVM_LOADER_H
#define BYTEVM_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "bytevm/common.h"
#include "bytevm/vm.h"

/* Fixed header size (DESIGN.md section 10). */
#define BVM_HEADER_SIZE 16u

/* Parses and loads a .bvm image (DESIGN.md section 10) into vm.
 *
 * On success: resets vm exactly as vm_init would (memory zeroed, CPU
 * reset), copies the code and data blobs to their header-specified
 * addresses, sets PC to entry_point, and computes stack_limit (DESIGN.md
 * section 6) from the loaded regions. This makes vm_load also the
 * correct implementation of the debugger's `reset` command (DESIGN.md
 * section 13): calling it again with the same bytes reproduces the
 * exact as-loaded state, discarding any runtime writes (including
 * self-modifying code and anything pushed to the stack).
 *
 * On failure: vm is left completely untouched. Every validation check
 * (magic, version, reserved, file length, address-space bounds,
 * entry_point range, code/data overlap -- DESIGN.md section 10) runs
 * before any state is mutated.
 *
 * data/size is the full raw file content (header + code + data) already
 * read into memory; this function does no file I/O of its own.
 *
 * Returns VM_ERR_BAD_BYTECODE_HEADER for any rejection condition. */
VmResult vm_load(VM *vm, const uint8_t *data, size_t size);

#endif /* BYTEVM_LOADER_H */
