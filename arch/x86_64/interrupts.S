/* SPDX-License-Identifier: Unlicense */
.section .text, "ax"
.code64
.extern interrupt_dispatch

.global isr_stub_0
.type isr_stub_0, @function
isr_stub_0:
    pushq $0\n    pushq $0
    jmp isr_common
.size isr_stub_0, . - isr_stub_0

.global isr_stub_1
.type isr_stub_1, @function
isr_stub_1:
    pushq $0\n    pushq $1
    jmp isr_common
.size isr_stub_1, . - isr_stub_1

.global isr_stub_2
.type isr_stub_2, @function
isr_stub_2:
    pushq $0\n    pushq $2
    jmp isr_common
.size isr_stub_2, . - isr_stub_2

.global isr_stub_3
.type isr_stub_3, @function
isr_stub_3:
    pushq $0\n    pushq $3
    jmp isr_common
.size isr_stub_3, . - isr_stub_3

.global isr_stub_4
.type isr_stub_4, @function
isr_stub_4:
    pushq $0\n    pushq $4
    jmp isr_common
.size isr_stub_4, . - isr_stub_4

.global isr_stub_5
.type isr_stub_5, @function
isr_stub_5:
    pushq $0\n    pushq $5
    jmp isr_common
.size isr_stub_5, . - isr_stub_5

.global isr_stub_6
.type isr_stub_6, @function
isr_stub_6:
    pushq $0\n    pushq $6
    jmp isr_common
.size isr_stub_6, . - isr_stub_6

.global isr_stub_7
.type isr_stub_7, @function
isr_stub_7:
    pushq $0\n    pushq $7
    jmp isr_common
.size isr_stub_7, . - isr_stub_7

.global isr_stub_8
.type isr_stub_8, @function
isr_stub_8:
    pushq $8
    jmp isr_common
.size isr_stub_8, . - isr_stub_8

.global isr_stub_9
.type isr_stub_9, @function
isr_stub_9:
    pushq $0\n    pushq $9
    jmp isr_common
.size isr_stub_9, . - isr_stub_9

.global isr_stub_10
.type isr_stub_10, @function
isr_stub_10:
    pushq $10
    jmp isr_common
.size isr_stub_10, . - isr_stub_10

.global isr_stub_11
.type isr_stub_11, @function
isr_stub_11:
    pushq $11
    jmp isr_common
.size isr_stub_11, . - isr_stub_11

.global isr_stub_12
.type isr_stub_12, @function
isr_stub_12:
    pushq $12
    jmp isr_common
.size isr_stub_12, . - isr_stub_12

.global isr_stub_13
.type isr_stub_13, @function
isr_stub_13:
    pushq $13
    jmp isr_common
.size isr_stub_13, . - isr_stub_13

.global isr_stub_14
.type isr_stub_14, @function
isr_stub_14:
    pushq $14
    jmp isr_common
.size isr_stub_14, . - isr_stub_14

.global isr_stub_15
.type isr_stub_15, @function
isr_stub_15:
    pushq $0\n    pushq $15
    jmp isr_common
.size isr_stub_15, . - isr_stub_15

.global isr_stub_16
.type isr_stub_16, @function
isr_stub_16:
    pushq $0\n    pushq $16
    jmp isr_common
.size isr_stub_16, . - isr_stub_16

.global isr_stub_17
.type isr_stub_17, @function
isr_stub_17:
    pushq $17
    jmp isr_common
.size isr_stub_17, . - isr_stub_17

.global isr_stub_18
.type isr_stub_18, @function
isr_stub_18:
    pushq $0\n    pushq $18
    jmp isr_common
.size isr_stub_18, . - isr_stub_18

.global isr_stub_19
.type isr_stub_19, @function
isr_stub_19:
    pushq $0\n    pushq $19
    jmp isr_common
.size isr_stub_19, . - isr_stub_19

.global isr_stub_20
.type isr_stub_20, @function
isr_stub_20:
    pushq $0\n    pushq $20
    jmp isr_common
.size isr_stub_20, . - isr_stub_20

.global isr_stub_21
.type isr_stub_21, @function
isr_stub_21:
    pushq $21
    jmp isr_common
.size isr_stub_21, . - isr_stub_21

.global isr_stub_22
.type isr_stub_22, @function
isr_stub_22:
    pushq $0\n    pushq $22
    jmp isr_common
.size isr_stub_22, . - isr_stub_22

.global isr_stub_23
.type isr_stub_23, @function
isr_stub_23:
    pushq $0\n    pushq $23
    jmp isr_common
.size isr_stub_23, . - isr_stub_23

.global isr_stub_24
.type isr_stub_24, @function
isr_stub_24:
    pushq $0\n    pushq $24
    jmp isr_common
.size isr_stub_24, . - isr_stub_24

.global isr_stub_25
.type isr_stub_25, @function
isr_stub_25:
    pushq $0\n    pushq $25
    jmp isr_common
.size isr_stub_25, . - isr_stub_25

.global isr_stub_26
.type isr_stub_26, @function
isr_stub_26:
    pushq $0\n    pushq $26
    jmp isr_common
.size isr_stub_26, . - isr_stub_26

.global isr_stub_27
.type isr_stub_27, @function
isr_stub_27:
    pushq $0\n    pushq $27
    jmp isr_common
.size isr_stub_27, . - isr_stub_27

.global isr_stub_28
.type isr_stub_28, @function
isr_stub_28:
    pushq $0\n    pushq $28
    jmp isr_common
.size isr_stub_28, . - isr_stub_28

.global isr_stub_29
.type isr_stub_29, @function
isr_stub_29:
    pushq $29
    jmp isr_common
.size isr_stub_29, . - isr_stub_29

.global isr_stub_30
.type isr_stub_30, @function
isr_stub_30:
    pushq $30
    jmp isr_common
.size isr_stub_30, . - isr_stub_30

.global isr_stub_31
.type isr_stub_31, @function
isr_stub_31:
    pushq $0\n    pushq $31
    jmp isr_common
.size isr_stub_31, . - isr_stub_31

.global isr_stub_32
.type isr_stub_32, @function
isr_stub_32:
    pushq $0\n    pushq $32
    jmp isr_common
.size isr_stub_32, . - isr_stub_32

.global isr_stub_33
.type isr_stub_33, @function
isr_stub_33:
    pushq $0\n    pushq $33
    jmp isr_common
.size isr_stub_33, . - isr_stub_33

.global isr_stub_34
.type isr_stub_34, @function
isr_stub_34:
    pushq $0\n    pushq $34
    jmp isr_common
.size isr_stub_34, . - isr_stub_34

.global isr_stub_35
.type isr_stub_35, @function
isr_stub_35:
    pushq $0\n    pushq $35
    jmp isr_common
.size isr_stub_35, . - isr_stub_35

.global isr_stub_36
.type isr_stub_36, @function
isr_stub_36:
    pushq $0\n    pushq $36
    jmp isr_common
.size isr_stub_36, . - isr_stub_36

.global isr_stub_37
.type isr_stub_37, @function
isr_stub_37:
    pushq $0\n    pushq $37
    jmp isr_common
.size isr_stub_37, . - isr_stub_37

.global isr_stub_38
.type isr_stub_38, @function
isr_stub_38:
    pushq $0\n    pushq $38
    jmp isr_common
.size isr_stub_38, . - isr_stub_38

.global isr_stub_39
.type isr_stub_39, @function
isr_stub_39:
    pushq $0\n    pushq $39
    jmp isr_common
.size isr_stub_39, . - isr_stub_39

.global isr_stub_40
.type isr_stub_40, @function
isr_stub_40:
    pushq $0\n    pushq $40
    jmp isr_common
.size isr_stub_40, . - isr_stub_40

.global isr_stub_41
.type isr_stub_41, @function
isr_stub_41:
    pushq $0\n    pushq $41
    jmp isr_common
.size isr_stub_41, . - isr_stub_41

.global isr_stub_42
.type isr_stub_42, @function
isr_stub_42:
    pushq $0\n    pushq $42
    jmp isr_common
.size isr_stub_42, . - isr_stub_42

.global isr_stub_43
.type isr_stub_43, @function
isr_stub_43:
    pushq $0\n    pushq $43
    jmp isr_common
.size isr_stub_43, . - isr_stub_43

.global isr_stub_44
.type isr_stub_44, @function
isr_stub_44:
    pushq $0\n    pushq $44
    jmp isr_common
.size isr_stub_44, . - isr_stub_44

.global isr_stub_45
.type isr_stub_45, @function
isr_stub_45:
    pushq $0\n    pushq $45
    jmp isr_common
.size isr_stub_45, . - isr_stub_45

.global isr_stub_46
.type isr_stub_46, @function
isr_stub_46:
    pushq $0\n    pushq $46
    jmp isr_common
.size isr_stub_46, . - isr_stub_46

.global isr_stub_47
.type isr_stub_47, @function
isr_stub_47:
    pushq $0\n    pushq $47
    jmp isr_common
.size isr_stub_47, . - isr_stub_47

.global isr_default_stub
.type isr_default_stub, @function
isr_default_stub:
    pushq $0
    pushq $255
    jmp isr_common
.size isr_default_stub, . - isr_default_stub

isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* R12 is callee-saved by the SysV ABI; its interrupted value is already
     * present in the saved frame. Keep the frame pointer across the C call. */
    movq %rsp, %r12
    movq %rsp, %rdi
    andq $-16, %rsp
    call interrupt_dispatch
    movq %r12, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp              /* vector + normalized error-code slot */
    iretq

.section .rodata
.align 8
.global isr_stub_table
.type isr_stub_table, @object
isr_stub_table:
    .quad isr_stub_0
    .quad isr_stub_1
    .quad isr_stub_2
    .quad isr_stub_3
    .quad isr_stub_4
    .quad isr_stub_5
    .quad isr_stub_6
    .quad isr_stub_7
    .quad isr_stub_8
    .quad isr_stub_9
    .quad isr_stub_10
    .quad isr_stub_11
    .quad isr_stub_12
    .quad isr_stub_13
    .quad isr_stub_14
    .quad isr_stub_15
    .quad isr_stub_16
    .quad isr_stub_17
    .quad isr_stub_18
    .quad isr_stub_19
    .quad isr_stub_20
    .quad isr_stub_21
    .quad isr_stub_22
    .quad isr_stub_23
    .quad isr_stub_24
    .quad isr_stub_25
    .quad isr_stub_26
    .quad isr_stub_27
    .quad isr_stub_28
    .quad isr_stub_29
    .quad isr_stub_30
    .quad isr_stub_31
    .quad isr_stub_32
    .quad isr_stub_33
    .quad isr_stub_34
    .quad isr_stub_35
    .quad isr_stub_36
    .quad isr_stub_37
    .quad isr_stub_38
    .quad isr_stub_39
    .quad isr_stub_40
    .quad isr_stub_41
    .quad isr_stub_42
    .quad isr_stub_43
    .quad isr_stub_44
    .quad isr_stub_45
    .quad isr_stub_46
    .quad isr_stub_47
.size isr_stub_table, . - isr_stub_table

.section .note.GNU-stack, "", @progbits
