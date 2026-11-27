# Trap entry for the M-mode monitor.
# On ANY trap the hardware jumps here (address in mtvec) with:
#   mepc   = PC of the instruction that trapped
#   mcause = why it trapped
#   mtval  = extra info (bad address / bad instruction bits)
#   mstatus.MPP = the mode we came from
# Every general register still holds the interrupted code's values,
# so we must save all of them before running any C code.

    .section .text
    .globl trap_vector
    .align 4                        # mtvec needs a 4-byte aligned address
trap_vector:
    # 1. Switch to the trap stack.
    #    mscratch holds the trap stack top. Swap it with sp:
    #    sp = trap stack top, mscratch = interrupted code's sp.
    csrrw sp, mscratch, sp

    # 2. Make room for 32 registers (32 x 8 = 256 bytes).
    #    Slot i at offset 8*i holds register x_i (slot 0 unused: x0 is always 0).
    addi  sp, sp, -256

    sd    x1,   1*8(sp)
    # x2 (sp) is saved below, from mscratch
    sd    x3,   3*8(sp)
    sd    x4,   4*8(sp)
    sd    x5,   5*8(sp)
    sd    x6,   6*8(sp)
    sd    x7,   7*8(sp)
    sd    x8,   8*8(sp)
    sd    x9,   9*8(sp)
    sd    x10, 10*8(sp)
    sd    x11, 11*8(sp)
    sd    x12, 12*8(sp)
    sd    x13, 13*8(sp)
    sd    x14, 14*8(sp)
    sd    x15, 15*8(sp)
    sd    x16, 16*8(sp)
    sd    x17, 17*8(sp)
    sd    x18, 18*8(sp)
    sd    x19, 19*8(sp)
    sd    x20, 20*8(sp)
    sd    x21, 21*8(sp)
    sd    x22, 22*8(sp)
    sd    x23, 23*8(sp)
    sd    x24, 24*8(sp)
    sd    x25, 25*8(sp)
    sd    x26, 26*8(sp)
    sd    x27, 27*8(sp)
    sd    x28, 28*8(sp)
    sd    x29, 29*8(sp)
    sd    x30, 30*8(sp)
    sd    x31, 31*8(sp)

    csrr  t0, mscratch              # interrupted code's original sp
    sd    t0,   2*8(sp)

    # 3. Call the C handler: trap_handler(struct trap_frame *tf)
    mv    a0, sp
    call  trap_handler

    # 4. Restore. First reset mscratch to the trap stack top,
    #    ready for the next trap.
    addi  t0, sp, 256
    csrw  mscratch, t0

    ld    x1,   1*8(sp)
    ld    x3,   3*8(sp)
    ld    x4,   4*8(sp)
    ld    x5,   5*8(sp)
    ld    x6,   6*8(sp)
    ld    x7,   7*8(sp)
    ld    x8,   8*8(sp)
    ld    x9,   9*8(sp)
    ld    x10, 10*8(sp)
    ld    x11, 11*8(sp)
    ld    x12, 12*8(sp)
    ld    x13, 13*8(sp)
    ld    x14, 14*8(sp)
    ld    x15, 15*8(sp)
    ld    x16, 16*8(sp)
    ld    x17, 17*8(sp)
    ld    x18, 18*8(sp)
    ld    x19, 19*8(sp)
    ld    x20, 20*8(sp)
    ld    x21, 21*8(sp)
    ld    x22, 22*8(sp)
    ld    x23, 23*8(sp)
    ld    x24, 24*8(sp)
    ld    x25, 25*8(sp)
    ld    x26, 26*8(sp)
    ld    x27, 27*8(sp)
    ld    x28, 28*8(sp)
    ld    x29, 29*8(sp)
    ld    x30, 30*8(sp)
    ld    x31, 31*8(sp)

    ld    sp,   2*8(sp)             # last: original sp (base and target are both sp)

    # 5. Return to mepc, in the mode stored in mstatus.MPP.
    mret
