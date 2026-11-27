# The verifier's code, embedded in the monitor image.
# The monitor copies it into trusted memory at boot, so the normal world
# never has a chance to supply or modify it.

    .section .rodata
    .globl verifier_blob_start
    .globl verifier_blob_end
    .balign 8
verifier_blob_start:
    .incbin "build/verifier.bin"
verifier_blob_end:
