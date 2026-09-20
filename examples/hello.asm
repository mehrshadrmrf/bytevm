; hello.asm -- prints "Hello!\n" one character at a time.
; Demonstrates: .data, .string, label references, [Rn] indirect
; addressing, a conditional-jump loop, and SYS PRINT_CHAR.

    MOV R1, message         ; R1 = pointer into the string
loop:
    LOAD R0, [R1]           ; R0 = the 16-bit word at R1...
    AND R0, 0x00FF          ; ...keep only the low byte (one character)
    JZ done                 ; a NUL byte ends the string
    SYS PRINT_CHAR
    ADD R1, 1               ; advance one byte
    JMP loop
done:
    HALT

.data
message:
    .string "Hello!\n"
