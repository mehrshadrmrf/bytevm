; disassembled from a .bvm image
; code: 32 bytes at 0x0000, data: 8 bytes at 0x0020

    MOV R1, 32
    LOAD R0, [R1]
    AND R0, 255
    JZ 0x001E
    SYS PRINT_CHAR
    ADD R1, 1
    JMP 0x0005
    HALT

.data
    .byte 0x48
    .byte 0x65
    .byte 0x6C
    .byte 0x6C
    .byte 0x6F
    .byte 0x21
    .byte 0x0A
    .byte 0x00
