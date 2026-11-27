# void enter_normal_world(uint64_t entry, uint64_t hartid, uint64_t dtb)
#
# Leave M-mode and start the normal world at `entry`.
# main() has already set mstatus.MPP = S, so mret drops to S-mode.
# The normal world receives a0 = hartid, a1 = dtb (the boot convention Linux expects).
# This never returns: the monitor only runs again when a trap brings it back.

    .section .text
    .globl enter_normal_world
enter_normal_world:
    csrw  mepc, a0
    mv    a0, a1
    mv    a1, a2
    mret
