; sort.asm -- bubble sort, in place, on a 6-element array, then prints
; it sorted. Expects: 1 2 3 5 8 9
;
; This closes a loop that started at the very first design review: the
; original scaffold's sort.asm was an empty placeholder because
; REG_INDIRECT addressing wasn't wired up to LOAD/STORE yet (DESIGN.md
; section 0, revision 0.1 -> 0.2, item 4) -- there was no way to index
; into an array at all. It works now.

outer:
    MOV R6, 0                ; swapped = false, this pass
    MOV R1, numbers          ; R1 = &numbers[0]
    MOV R2, 5                ; 5 adjacent pairs to compare (6 elements)

inner:
    LOAD R3, [R1]            ; R3 = numbers[i]
    ADD R1, 2                ; R1 = &numbers[i+1]
    LOAD R4, [R1]            ; R4 = numbers[i+1]

    CMP R3, R4
    JLE no_swap               ; already in order -- nothing to do

    STORE [R1], R3            ; numbers[i+1] = R3
    SUB R1, 2
    STORE [R1], R4            ; numbers[i]   = R4
    ADD R1, 2
    MOV R6, 1                 ; a swap happened this pass

no_swap:
    SUB R2, 1
    JNZ inner

    CMP R6, 0
    JNZ outer                  ; keep passing while swaps still happen

    ; print the sorted array, space-separated
    MOV R1, numbers
    MOV R2, 6
print_loop:
    LOAD R0, [R1]
    SYS PRINT_INT
    MOV R0, 32                 ; ' '
    SYS PRINT_CHAR
    ADD R1, 2
    SUB R2, 1
    JNZ print_loop

    MOV R0, 10                 ; '\n'
    SYS PRINT_CHAR
    MOV R0, 0
    HALT

.data
numbers:
    .word 5
    .word 3
    .word 8
    .word 1
    .word 9
    .word 2
