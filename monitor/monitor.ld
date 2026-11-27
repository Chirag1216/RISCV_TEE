OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
  MON_RX (rx) : ORIGIN = 0x80000000, LENGTH = 256K
  MON_RW (rw) : ORIGIN = 0x80040000, LENGTH = 256K
}

SECTIONS {
  .text : {
    KEEP(*(.text.start))
    *(.text .text.*)
  } > MON_RX

  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > MON_RX

  .data : { *(.data .data.* .sdata .sdata.*) } > MON_RW

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.sbss .sbss.* .bss .bss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > MON_RW

  .stack (NOLOAD) : ALIGN(16) {
    __stack_bottom = .;
    . += 16K;                       /* boot stack: start.S + main */
    __stack_top = .;
    __trap_stack_bottom = .;
    . += 4K;                        /* trap stack: used only inside the trap handler */
    __trap_stack_top = .;
  } > MON_RW
}
