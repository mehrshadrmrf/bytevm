#ifndef BYTEVM_COMMON_H
#define BYTEVM_COMMON_H

/* Result/error codes shared by every VM subsystem (DESIGN.md section 9).
 * On any error other than VM_OK, the VM sets halted = true and records
 * both the code and the PC where it occurred -- it never corrupts memory
 * or crashes the host process silently. */
typedef enum {
    VM_OK = 0,
    VM_ERR_INVALID_OPCODE,
    VM_ERR_INVALID_MODE,
    VM_ERR_DIV_BY_ZERO,
    VM_ERR_STACK_OVERFLOW,
    VM_ERR_STACK_UNDERFLOW,
    VM_ERR_BAD_BYTECODE_HEADER,
    VM_ERR_INVALID_SYSCALL,
} VmResult;

/* Human-readable description of a VmResult, for the CLI and debugger.
 * Never returns NULL. */
const char *vm_result_str(VmResult result);

#endif /* BYTEVM_COMMON_H */
