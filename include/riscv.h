#ifndef RISCV_H
#define RISCV_H

#include <stdint.h>

/* Read / write a CSR by name, e.g. csr_read(mcause), csr_write(mtvec, addr) */
#define csr_read(csr) ({                                        \
    uint64_t __v;                                               \
    asm volatile ("csrr %0, " #csr : "=r"(__v));                \
    __v;                                                        \
})

#define csr_write(csr, val) ({                                  \
    asm volatile ("csrw " #csr ", %0" :: "r"((uint64_t)(val))); \
})

#define csr_set(csr, bits) ({                                   \
    asm volatile ("csrs " #csr ", %0" :: "r"((uint64_t)(bits))); \
})

#define csr_clear(csr, bits) ({                                 \
    asm volatile ("csrc " #csr ", %0" :: "r"((uint64_t)(bits))); \
})

/* mstatus.MPP (bits 12:11): the privilege mode a trap came from / mret returns to */
#define MSTATUS_MPP_SHIFT       11
#define MSTATUS_MPP_MASK        (3UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_U           (0UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_S           (1UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_M           (3UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPIE            (1UL << 7)
#define MSTATUS_FS_MASK         (3UL << 13)     /* floating-point unit state: 0 = off */
#define MSTATUS_VS_MASK         (3UL << 9)      /* vector unit state: 0 = off */

/* mie / mip: machine timer interrupt */
#define MIE_MTIE                (1UL << 7)
#define IRQ_M_TIMER             7

/* Interrupt bits in mip / mie / mideleg */
#define IP_SSIP                 (1UL << 1)      /* supervisor software interrupt */
#define IP_STIP                 (1UL << 5)      /* supervisor timer interrupt    */
#define IP_SEIP                 (1UL << 9)      /* supervisor external interrupt */

/* menvcfg: features the monitor allows the normal world to use */
#define MENVCFG_CBIE            (3UL << 4)      /* cache-block invalidate instructions */
#define MENVCFG_CBCFE           (1UL << 6)      /* cache-block clean/flush             */
#define MENVCFG_CBZE            (1UL << 7)      /* cache-block zero                    */
#define MENVCFG_PBMTE           (1UL << 62)     /* page-based memory types             */
#define MENVCFG_STCE            (1UL << 63)     /* stimecmp: S-mode's own timer        */

/* CSRs the assembler may not know by name without extra -march flags */
#define CSR_STIMECMP            0x14d
#define CSR_MENVCFG             0x30a

/* mcause: top bit set = interrupt, clear = exception */
#define MCAUSE_INTERRUPT        (1UL << 63)

/* Exception codes (mcause with top bit clear) */
#define EXC_INSN_MISALIGNED     0
#define EXC_INSN_ACCESS_FAULT   1
#define EXC_ILLEGAL_INSN        2
#define EXC_BREAKPOINT          3
#define EXC_LOAD_MISALIGNED     4
#define EXC_LOAD_ACCESS_FAULT   5
#define EXC_STORE_MISALIGNED    6
#define EXC_STORE_ACCESS_FAULT  7
#define EXC_ECALL_U             8
#define EXC_ECALL_S             9
#define EXC_ECALL_M             11
#define EXC_INSN_PAGE_FAULT     12
#define EXC_LOAD_PAGE_FAULT     13
#define EXC_STORE_PAGE_FAULT    15

/* misa: one bit per extension letter, bit 0 = 'A' ... bit 25 = 'Z' */
#define MISA_EXT(letter)        (1UL << ((letter) - 'A'))

#endif
