OUTPUT_ARCH(riscv)
ENTRY(_start)

/* The normal-world test payload sits where Linux will later be loaded. */
MEMORY {
  RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 1M
}

SECTIONS {
  .text : {
    KEEP(*(.text.start))
    *(.text .text.*)
  } > RAM
  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > RAM
  .data   : { *(.data .data.* .sdata .sdata.*) } > RAM
  .bss    : ALIGN(16) {
    *(.sbss .sbss.* .bss .bss.* COMMON)
  } > RAM
  .stack (NOLOAD) : ALIGN(16) {
    . += 16K;
    __payload_stack_top = .;
  } > RAM
}
