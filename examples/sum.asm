; sum.asm -- sums an array stored in the data section and prints the
; total (expects 150). This is the program that was impossible to write
; before REG_INDIRECT addressing was wired up to LOAD/STORE (see
; DESIGN.md section 0, revision 0.1 -> 0.2, item 4): indexing an array
; needs a runtime-computed address, not a compile-time literal.

    MOV R0, 0               ; running total
    MOV R1, numbers         ; R1 = pointer to the current element
    MOV R2, 5               ; R2 = elements remaining
sum_loop:
    LOAD R3, [R1]           ; R3 = *R1  -- the indirect load
    ADD R0, R3
    ADD R1, 2               ; advance one 16-bit word
    SUB R2, 1
    JNZ sum_loop

    SYS PRINT_INT
    MOV R0, 10              ; '\n'
    SYS PRINT_CHAR
    MOV R0, 0               ; exit code 0 (DESIGN.md section 8.7)
    HALT

.data
numbers:
    .word 10
    .word 20
    .word 30
    .word 40
    .word 50
