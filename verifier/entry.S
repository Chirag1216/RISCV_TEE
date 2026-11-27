# Verifier entry point. This is the whole "runtime".
# The monitor enters here in U-mode with a0 = pointer to the request.
# The ecall at the end returns the verdict (in a0) to the monitor.

    .section .text.start
    .globl _start
_start:
    la    sp, __vstack_top
    call  verify
    ecall
1:  j     1b                    # never reached
