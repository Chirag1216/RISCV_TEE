OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
  TCODE (rx) : ORIGIN = 0xFFE00000, LENGTH = 512K   /* read + execute in the trusted view */
  TDATA (rw) : ORIGIN = 0xFFE80000, LENGTH = 512K   /* read + write in the trusted view   */
}

SECTIONS {
  .text : {
    KEEP(*(.text.start))        /* _start must be the first instruction */
    *(.text .text.*)
  } > TCODE

  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > TCODE

  /* Initialised data is not supported: the monitor only loads code and zeroes TDATA. */
  .data : { *(.data .data.* .sdata .sdata.*) } > TDATA
  ASSERT(SIZEOF(.data) == 0, "verifier must not have initialised data")

  .mailbox (NOLOAD) : {
    . += 4K;                    /* the monitor writes each request here */
  } > TDATA

  .bss (NOLOAD) : ALIGN(16) {
    *(.sbss .sbss.* .bss .bss.* COMMON)
  } > TDATA

  .stack (NOLOAD) : ALIGN(16) {
    . += 16K;
    __vstack_top = .;
  } > TDATA
}
