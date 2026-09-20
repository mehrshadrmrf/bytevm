; factorial.asm -- computes 5! iteratively and prints it (expects 120).
; Demonstrates: CALL/RET, the R0-R3 calling convention (DESIGN.md
; section 8.6), PUSH/POP to preserve a register across a call, and
; SYS PRINT_INT.

    MOV R0, 5               ; argument: n = 5
    CALL factorial          ; returns n! in R0
    SYS PRINT_INT
    MOV R0, 10              ; '\n'
    SYS PRINT_CHAR
    MOV R0, 0               ; exit code 0 (DESIGN.md section 8.7)
    HALT

; factorial(n in R0) -> n! in R0
; Clobbers R1/R2 freely (caller-saved), but preserves R3 by pushing it,
; to show the "callers must PUSH what they need" convention from the
; callee side too.
factorial:
    PUSH R3
    MOV R1, R0              ; R1 = counter, counting down
    MOV R0, 1               ; R0 = accumulator
fact_loop:
    CMP R1, 0
    JZ fact_done
    MUL R0, R1
    SUB R1, 1
    JMP fact_loop
fact_done:
    POP R3
    RET
