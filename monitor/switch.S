# The world switch: running the verifier and getting back.
#
# long run_verifier(uint64_t entry, uint64_t arg)
#   Called by the monitor (world.c) while it is handling the normal world's ecall.
#   Starts the verifier in U-mode and "returns" only when verifier_return() is
#   called from the trap that ends the verifier's run.
#
# void verifier_return(long verdict)
#   Called from the trap handler. Resumes run_verifier's caller with the verdict.
#
# Together they work like setjmp/longjmp: run_verifier remembers where the
# monitor was, verifier_return goes back there.

    .section .text

    .globl run_verifier
run_verifier:
    # Save what the C caller expects to survive a call: ra and s0-s11.
    addi  sp, sp, -112
    sd    ra,    0(sp)
    sd    s0,    8(sp)
    sd    s1,   16(sp)
    sd    s2,   24(sp)
    sd    s3,   32(sp)
    sd    s4,   40(sp)
    sd    s5,   48(sp)
    sd    s6,   56(sp)
    sd    s7,   64(sp)
    sd    s8,   72(sp)
    sd    s9,   80(sp)
    sd    s10,  88(sp)
    sd    s11,  96(sp)

    la    t0, resume_sp
    sd    sp, 0(t0)             # remember where to come back to
    csrw  mscratch, sp          # the verifier's trap will use the stack below this point

    csrw  mepc, a0              # verifier entry point
    mv    a0, a1                # its one argument

    # Clear every other register: nothing from the monitor or the normal
    # world leaks into the verifier.
    li    x1, 0
    li    x2, 0
    li    x3, 0
    li    x4, 0
    li    x5, 0
    li    x6, 0
    li    x7, 0
    li    x8, 0
    li    x9, 0
    li    x11, 0
    li    x12, 0
    li    x13, 0
    li    x14, 0
    li    x15, 0
    li    x16, 0
    li    x17, 0
    li    x18, 0
    li    x19, 0
    li    x20, 0
    li    x21, 0
    li    x22, 0
    li    x23, 0
    li    x24, 0
    li    x25, 0
    li    x26, 0
    li    x27, 0
    li    x28, 0
    li    x29, 0
    li    x30, 0
    li    x31, 0

    mret                        # to U-mode (mstatus.MPP was set by the caller)

    .globl verifier_return
verifier_return:
    la    t0, resume_sp
    ld    sp, 0(t0)             # drop the verifier's trap frame, back to run_verifier's frame
    ld    ra,    0(sp)
    ld    s0,    8(sp)
    ld    s1,   16(sp)
    ld    s2,   24(sp)
    ld    s3,   32(sp)
    ld    s4,   40(sp)
    ld    s5,   48(sp)
    ld    s6,   56(sp)
    ld    s7,   64(sp)
    ld    s8,   72(sp)
    ld    s9,   80(sp)
    ld    s10,  88(sp)
    ld    s11,  96(sp)
    addi  sp, sp, 112
    ret                         # a0 = verdict: returns to run_verifier's caller

    .section .bss
    .balign 8
resume_sp:
    .dword 0
