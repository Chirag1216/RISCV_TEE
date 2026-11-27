# Monitor entry point. QEMU jumps here in M-mode with:
#   a0 = hart id, a1 = address of the device tree (dtb)

    .section .text.start
    .globl _start
_start:
    mv    s1, a1                 # keep the dtb pointer safe

    la    sp, __stack_top        # boot stack

    la    t0, __bss_start        # zero .bss
    la    t1, __bss_end
1:  bgeu  t0, t1, 2f
    sd    zero, 0(t0)
    addi  t0, t0, 8
    j     1b
2:
    mv    a0, s1                 # main(dtb)
    call  main

3:  wfi                          # main should never return
    j     3b
