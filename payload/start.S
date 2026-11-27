# Entry point of the normal-world test payload (runs in S-mode).

    .section .text.start
    .globl _start
_start:
    la    sp, __payload_stack_top
    la    t0, s_trap
    csrw  stvec, t0                 # our own S-mode trap handler
    call  payload_main
1:  wfi
    j     1b

# S-mode trap handler.
# A PMP violation is delivered here as an access fault, exactly as a real
# kernel would receive it. Count it and skip the faulting instruction.
    .section .text
    .balign 4
s_trap:
    addi  sp, sp, -16
    sd    t0, 0(sp)
    sd    t1, 8(sp)

    la    t0, fault_count
    ld    t1, 0(t0)
    addi  t1, t1, 1
    sd    t1, 0(t0)

    csrr  t0, sepc
    lhu   t1, 0(t0)                 # low 2 bits == 3 means a 4-byte instruction
    andi  t1, t1, 3
    addi  t0, t0, 2
    addi  t1, t1, -3
    bnez  t1, 2f
    addi  t0, t0, 2
2:  csrw  sepc, t0

    ld    t0, 0(sp)
    ld    t1, 8(sp)
    addi  sp, sp, 16
    sret
