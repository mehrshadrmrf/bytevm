#include "bytevm/common.h"

const char *vm_result_str(VmResult result)
{
    switch (result) {
    case VM_OK:
        return "ok";
    case VM_ERR_INVALID_OPCODE:
        return "invalid opcode";
    case VM_ERR_INVALID_MODE:
        return "invalid addressing mode";
    case VM_ERR_DIV_BY_ZERO:
        return "division by zero";
    case VM_ERR_STACK_OVERFLOW:
        return "stack overflow";
    case VM_ERR_STACK_UNDERFLOW:
        return "stack underflow";
    case VM_ERR_BAD_BYTECODE_HEADER:
        return "bad bytecode header";
    case VM_ERR_INVALID_SYSCALL:
        return "invalid syscall code";
    }
    return "unknown error";
}
