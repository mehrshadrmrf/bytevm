#include "bytevm/vm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bytevm/instruction.h"

void vm_init(VM *vm)
{
    cpu_init(&vm->cpu);
    mem_init(&vm->memory);
    vm->halted = false;
    vm->last_error = VM_OK;
    vm->error_pc = 0;
    vm->stack_limit = 0;
}

/* Resolves a REG/IMM16 operand to its 16-bit value (DESIGN.md section
 * 7.3 -- these are the only two modes ever legal for an arithmetic
 * source operand, and for CMP's operands specifically). Unreachable for
 * any other mode if instruction_decode enforced the legality table
 * correctly. */
static uint16_t operand_read(const VM *vm, const Operand *op)
{
    switch (op->mode) {
    case MODE_REG:
        return vm->cpu.r[op->value];
    case MODE_IMM16:
        return op->value;
    default:
        return 0;
    }
}

/* Resolves a MEM_DIRECT/REG_INDIRECT operand to the memory address it
 * names (DESIGN.md section 7.3 -- the only two modes ever legal for
 * LOAD/STORE's address operand). */
static uint16_t operand_effective_address(const VM *vm, const Operand *op)
{
    switch (op->mode) {
    case MODE_MEM_DIRECT:
        return op->value;
    case MODE_REG_INDIRECT:
        return vm->cpu.r[op->value];
    default:
        return 0;
    }
}

/* DESIGN.md section 8.3's shift-count table. Never executes a native C
 * `<<`/`>>` with an out-of-range count -- n is branched on explicitly
 * for every range the table defines. */
static uint16_t shift_left(uint16_t value, uint16_t n, bool *carry_out)
{
    if (n == 0) {
        *carry_out = false;
        return value;
    }
    if (n > 16) {
        *carry_out = false;
        return 0;
    }
    *carry_out = ((value >> (16 - n)) & 0x1u) != 0;
    return (n == 16) ? 0 : (uint16_t)(value << n);
}

static uint16_t shift_right(uint16_t value, uint16_t n, bool *carry_out)
{
    if (n == 0) {
        *carry_out = false;
        return value;
    }
    if (n > 16) {
        *carry_out = false;
        return 0;
    }
    *carry_out = ((value >> (n - 1)) & 0x1u) != 0;
    return (n == 16) ? 0 : (uint16_t)(value >> n);
}

/* ---- Data movement (Phase 2) ---- */

static VmResult exec_mov(VM *vm, const Instruction *instr)
{
    vm->cpu.r[instr->op1.value] = operand_read(vm, &instr->op2);
    return VM_OK;
}

static VmResult exec_load(VM *vm, const Instruction *instr)
{
    uint16_t addr = operand_effective_address(vm, &instr->op2);
    vm->cpu.r[instr->op1.value] = mem_read16(&vm->memory, addr);
    return VM_OK;
}

static VmResult exec_store(VM *vm, const Instruction *instr)
{
    uint16_t addr = operand_effective_address(vm, &instr->op1);
    uint16_t val = vm->cpu.r[instr->op2.value];
    mem_write16(&vm->memory, addr, val);
    return VM_OK;
}

/* ---- Arithmetic (DESIGN.md section 8.2) ---- */

static VmResult exec_add(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t b = operand_read(vm, &instr->op2);
    uint32_t sum = (uint32_t)a + (uint32_t)b;
    uint16_t result = (uint16_t)sum;

    cpu_set_flag(&vm->cpu, CPU_FLAG_C, (sum >> 16) != 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_O, ((a ^ result) & (b ^ result) & 0x8000u) != 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_sub(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t b = operand_read(vm, &instr->op2);
    uint16_t result = (uint16_t)(a - b);

    cpu_set_flag(&vm->cpu, CPU_FLAG_C, a < b);
    cpu_set_flag(&vm->cpu, CPU_FLAG_O, ((a ^ b) & (a ^ result) & 0x8000u) != 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_mul(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t b = operand_read(vm, &instr->op2);

    uint32_t product = (uint32_t)a * (uint32_t)b;
    uint16_t result = (uint16_t)(product & 0xFFFFu);

    /* Separate signed-overflow computation -- not the same math as the
     * unsigned-truncation check above, and not the ADD/SUB overflow
     * formula either (DESIGN.md section 8.2). */
    int32_t signed_product = (int32_t)(int16_t)a * (int32_t)(int16_t)b;

    cpu_set_flag(&vm->cpu, CPU_FLAG_C, (product >> 16) != 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_O, signed_product < INT16_MIN || signed_product > INT16_MAX);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_div(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t b = operand_read(vm, &instr->op2);

    if (b == 0) {
        return VM_ERR_DIV_BY_ZERO;
    }

    uint16_t result = (uint16_t)(a / b);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    /* C and O are left unmodified -- neither has a meaning for unsigned
     * division (DESIGN.md section 8.2). */
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_mod(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t b = operand_read(vm, &instr->op2);

    if (b == 0) {
        return VM_ERR_DIV_BY_ZERO;
    }

    uint16_t result = (uint16_t)(a % b);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

/* ---- Bitwise (DESIGN.md section 8.3) ---- */

static VmResult exec_and(VM *vm, const Instruction *instr)
{
    uint16_t result = (uint16_t)(vm->cpu.r[instr->op1.value] & operand_read(vm, &instr->op2));
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_or(VM *vm, const Instruction *instr)
{
    uint16_t result = (uint16_t)(vm->cpu.r[instr->op1.value] | operand_read(vm, &instr->op2));
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_xor(VM *vm, const Instruction *instr)
{
    uint16_t result = (uint16_t)(vm->cpu.r[instr->op1.value] ^ operand_read(vm, &instr->op2));
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_not(VM *vm, const Instruction *instr)
{
    uint16_t result = (uint16_t)(~vm->cpu.r[instr->op1.value]);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_shl(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t n = operand_read(vm, &instr->op2);
    bool carry;
    uint16_t result = shift_left(a, n, &carry);

    cpu_set_flag(&vm->cpu, CPU_FLAG_C, carry);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

static VmResult exec_shr(VM *vm, const Instruction *instr)
{
    uint16_t a = vm->cpu.r[instr->op1.value];
    uint16_t n = operand_read(vm, &instr->op2);
    bool carry;
    uint16_t result = shift_right(a, n, &carry);

    cpu_set_flag(&vm->cpu, CPU_FLAG_C, carry);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    vm->cpu.r[instr->op1.value] = result;
    return VM_OK;
}

/* ---- Comparison (DESIGN.md section 8.4) ----
 * Reuses SUB's exact flag formula but never writes a register -- both
 * operands may be REG or IMM16 (section 7.3), unlike SUB whose op1 must
 * be REG, so both go through operand_read rather than direct r[] access. */
static VmResult exec_cmp(VM *vm, const Instruction *instr)
{
    uint16_t a = operand_read(vm, &instr->op1);
    uint16_t b = operand_read(vm, &instr->op2);
    uint16_t result = (uint16_t)(a - b);

    cpu_set_flag(&vm->cpu, CPU_FLAG_C, a < b);
    cpu_set_flag(&vm->cpu, CPU_FLAG_O, ((a ^ b) & (a ^ result) & 0x8000u) != 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_Z, result == 0);
    cpu_set_flag(&vm->cpu, CPU_FLAG_S, (result & 0x8000u) != 0);
    return VM_OK;
}

/* ---- Control flow (DESIGN.md section 8.5) ----
 * Each conditional jump's condition matches section 5's table exactly.
 * Unconditional JMP just overwrites PC outright. */

static VmResult exec_jmp(VM *vm, const Instruction *instr)
{
    vm->cpu.pc = instr->op1.value;
    return VM_OK;
}

static VmResult exec_jz(VM *vm, const Instruction *instr)
{
    if (cpu_get_flag(&vm->cpu, CPU_FLAG_Z)) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jnz(VM *vm, const Instruction *instr)
{
    if (!cpu_get_flag(&vm->cpu, CPU_FLAG_Z)) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jg(VM *vm, const Instruction *instr)
{
    bool z = cpu_get_flag(&vm->cpu, CPU_FLAG_Z);
    bool s = cpu_get_flag(&vm->cpu, CPU_FLAG_S);
    bool o = cpu_get_flag(&vm->cpu, CPU_FLAG_O);
    if (!z && s == o) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jge(VM *vm, const Instruction *instr)
{
    bool s = cpu_get_flag(&vm->cpu, CPU_FLAG_S);
    bool o = cpu_get_flag(&vm->cpu, CPU_FLAG_O);
    if (s == o) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jl(VM *vm, const Instruction *instr)
{
    bool s = cpu_get_flag(&vm->cpu, CPU_FLAG_S);
    bool o = cpu_get_flag(&vm->cpu, CPU_FLAG_O);
    if (s != o) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jle(VM *vm, const Instruction *instr)
{
    bool z = cpu_get_flag(&vm->cpu, CPU_FLAG_Z);
    bool s = cpu_get_flag(&vm->cpu, CPU_FLAG_S);
    bool o = cpu_get_flag(&vm->cpu, CPU_FLAG_O);
    if (z || s != o) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jb(VM *vm, const Instruction *instr)
{
    if (cpu_get_flag(&vm->cpu, CPU_FLAG_C)) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jae(VM *vm, const Instruction *instr)
{
    if (!cpu_get_flag(&vm->cpu, CPU_FLAG_C)) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_ja(VM *vm, const Instruction *instr)
{
    bool c = cpu_get_flag(&vm->cpu, CPU_FLAG_C);
    bool z = cpu_get_flag(&vm->cpu, CPU_FLAG_Z);
    if (!c && !z) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

static VmResult exec_jbe(VM *vm, const Instruction *instr)
{
    bool c = cpu_get_flag(&vm->cpu, CPU_FLAG_C);
    bool z = cpu_get_flag(&vm->cpu, CPU_FLAG_Z);
    if (c || z) {
        vm->cpu.pc = instr->op1.value;
    }
    return VM_OK;
}

/* ---- Stack (DESIGN.md section 6, section 8.1) ---- */

static VmResult exec_push(VM *vm, const Instruction *instr)
{
    uint16_t value = operand_read(vm, &instr->op1); /* REG or IMM16, section 7.3 */
    uint16_t sp = vm->cpu.sp;

    /* Overflow check happens before the write/decrement, per section 6's
     * exact formula. sp < 2 is checked first so `sp - 2` below never
     * wraps -- short-circuit `||` guarantees the second operand isn't
     * evaluated when sp < 2. */
    if (sp < 2 || (uint16_t)(sp - 2) < vm->stack_limit) {
        return VM_ERR_STACK_OVERFLOW;
    }

    mem_write16(&vm->memory, sp, value);
    vm->cpu.sp = (uint16_t)(sp - 2);
    return VM_OK;
}

static VmResult exec_pop(VM *vm, const Instruction *instr)
{
    /* CPU_INITIAL_SP (0xFFFE) is the empty-stack sentinel -- section 6. */
    if (vm->cpu.sp == CPU_INITIAL_SP) {
        return VM_ERR_STACK_UNDERFLOW;
    }

    uint16_t new_sp = (uint16_t)(vm->cpu.sp + 2);
    vm->cpu.r[instr->op1.value] = mem_read16(&vm->memory, new_sp);
    vm->cpu.sp = new_sp;
    return VM_OK;
}

/* ---- Functions (DESIGN.md section 8.6) ----
 * Mirror PUSH/POP exactly (same overflow/underflow rules, section 6) --
 * CALL pushes PC, RET pops it back. `PC` here is already advanced past
 * CALL's own encoded bytes by the fetch step (section 2.1), so it's
 * always the address of the instruction immediately following CALL. */

static VmResult exec_call(VM *vm, const Instruction *instr)
{
    uint16_t sp = vm->cpu.sp;

    if (sp < 2 || (uint16_t)(sp - 2) < vm->stack_limit) {
        return VM_ERR_STACK_OVERFLOW;
    }

    mem_write16(&vm->memory, sp, vm->cpu.pc);
    vm->cpu.sp = (uint16_t)(sp - 2);
    vm->cpu.pc = instr->op1.value;
    return VM_OK;
}

static VmResult exec_ret(VM *vm, const Instruction *instr)
{
    (void)instr; /* RET has no operands */

    if (vm->cpu.sp == CPU_INITIAL_SP) {
        return VM_ERR_STACK_UNDERFLOW;
    }

    uint16_t new_sp = (uint16_t)(vm->cpu.sp + 2);
    vm->cpu.pc = mem_read16(&vm->memory, new_sp);
    vm->cpu.sp = new_sp;
    return VM_OK;
}

/* ---- System calls (DESIGN.md section 8.7) ---- */

/* Skips leading whitespace, reads an optional '-' and one or more
 * decimal digits, and leaves the first non-digit character (if any)
 * unconsumed on the stream. Returns 0 if no digits were found (EOF or a
 * non-digit right away) -- malformed/absent input is an environment
 * condition, not a VM fault, so this never fails. The magnitude
 * accumulates in an *unsigned* long specifically so arbitrarily many
 * digits never triggers signed-overflow UB; masking its low 16 bits at
 * the end is equivalent to "truncated modulo 65536" for any number of
 * digits, since 65536 divides every power-of-two modulus unsigned
 * arithmetic wraps at. */
static uint16_t read_int_from_stdin(void)
{
    int c;
    do {
        c = getchar();
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');

    bool negative = false;
    if (c == '-') {
        negative = true;
        c = getchar();
    }

    bool has_digit = false;
    unsigned long magnitude = 0;
    while (c >= '0' && c <= '9') {
        has_digit = true;
        magnitude = magnitude * 10u + (unsigned long)(c - '0');
        c = getchar();
    }

    if (c != EOF) {
        ungetc(c, stdin);
    }

    if (!has_digit) {
        return 0;
    }

    uint16_t low16 = (uint16_t)(magnitude & 0xFFFFu);
    return negative ? (uint16_t)(0u - low16) : low16;
}

static VmResult exec_sys(VM *vm, const Instruction *instr)
{
    uint16_t code = instr->op1.value; /* IMM8, section 7.1/8.7 */

    switch (code) {
    case 0x01: /* PRINT_INT */
        printf("%d", (int16_t)vm->cpu.r[0]);
        return VM_OK;
    case 0x02: /* PRINT_CHAR */
        putchar((int)(vm->cpu.r[0] & 0xFFu));
        return VM_OK;
    case 0x03: /* READ_INT */
        vm->cpu.r[0] = read_int_from_stdin();
        return VM_OK;
    case 0x04: /* EXIT -- R0 already holds the VM-level exit code;
                * the host-exit-code mapping (R0 & 0xFF) is the
                * caller's job once the VM halts (section 8.7). */
        vm->halted = true;
        return VM_OK;
    default:
        return VM_ERR_INVALID_SYSCALL;
    }
}

/* DESIGN.md section 2.1 step 3 ("Execute"). As of Phase 5, every opcode
 * in the ISA has real semantics -- there is no longer an "unimplemented"
 * fallthrough group. The switch still lists every Opcode value
 * explicitly (no `default`) so a *new* opcode added in some future ISA
 * revision would be a compiler warning (-Wswitch), not a silent gap. */
static VmResult instruction_execute(VM *vm, const Instruction *instr)
{
    switch (instr->opcode) {
    case OP_NOP:
        return VM_OK;
    case OP_MOV:
        return exec_mov(vm, instr);
    case OP_ADD:
        return exec_add(vm, instr);
    case OP_SUB:
        return exec_sub(vm, instr);
    case OP_MUL:
        return exec_mul(vm, instr);
    case OP_DIV:
        return exec_div(vm, instr);
    case OP_MOD:
        return exec_mod(vm, instr);
    case OP_AND:
        return exec_and(vm, instr);
    case OP_OR:
        return exec_or(vm, instr);
    case OP_XOR:
        return exec_xor(vm, instr);
    case OP_NOT:
        return exec_not(vm, instr);
    case OP_SHL:
        return exec_shl(vm, instr);
    case OP_SHR:
        return exec_shr(vm, instr);
    case OP_LOAD:
        return exec_load(vm, instr);
    case OP_STORE:
        return exec_store(vm, instr);
    case OP_PUSH:
        return exec_push(vm, instr);
    case OP_POP:
        return exec_pop(vm, instr);
    case OP_CMP:
        return exec_cmp(vm, instr);
    case OP_JMP:
        return exec_jmp(vm, instr);
    case OP_JZ:
        return exec_jz(vm, instr);
    case OP_JNZ:
        return exec_jnz(vm, instr);
    case OP_JG:
        return exec_jg(vm, instr);
    case OP_JGE:
        return exec_jge(vm, instr);
    case OP_JL:
        return exec_jl(vm, instr);
    case OP_JLE:
        return exec_jle(vm, instr);
    case OP_JB:
        return exec_jb(vm, instr);
    case OP_JAE:
        return exec_jae(vm, instr);
    case OP_JA:
        return exec_ja(vm, instr);
    case OP_JBE:
        return exec_jbe(vm, instr);
    case OP_CALL:
        return exec_call(vm, instr);
    case OP_RET:
        return exec_ret(vm, instr);
    case OP_SYS:
        return exec_sys(vm, instr);
    case OP_HALT:
        vm->halted = true;
        return VM_OK;
    }

    return VM_ERR_INVALID_OPCODE; /* unreachable -- switch above is exhaustive */
}

VmResult vm_step(VM *vm)
{
    if (vm->halted) {
        return vm->last_error;
    }

    uint16_t fault_pc = vm->cpu.pc;
    Instruction instr;
    VmResult result = instruction_decode(&vm->memory, &vm->cpu.pc, &instr);

    if (result == VM_OK) {
        result = instruction_execute(vm, &instr);
    }

    if (result != VM_OK) {
        vm->halted = true;
        vm->last_error = result;
        vm->error_pc = fault_pc;
    }

    return result;
}
