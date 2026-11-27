#ifndef LAYOUT_H
#define LAYOUT_H

/* Physical memory map (QEMU virt, 2 GiB RAM at 0x8000_0000). One place for every address. */

#define RAM_BASE        0x80000000UL
#define RAM_SIZE        0x80000000UL    /* 2 GiB */

#define MON_BASE        0x80000000UL    /* monitor code + data            */
#define MON_SIZE        0x00080000UL    /* 512K                           */

#define NORMAL_BASE     0x80200000UL    /* normal world entry: Linux or the test payload */

/* Trusted world: the top 2 MiB of RAM. */
#define TCODE_BASE      0xFFE00000UL    /* trusted verifier code          */
#define TCODE_SIZE      0x00080000UL    /* 512K                           */
#define TDATA_BASE      0xFFE80000UL    /* trusted verifier data + stack  */
#define TDATA_SIZE      0x00080000UL    /* 512K                           */
#define ACT_BASE        0xFFF00000UL    /* mock actuator                  */
#define ACT_SIZE        0x00100000UL    /* 1M                             */
#define TRUSTED_BASE    TCODE_BASE
#define TRUSTED_END     (ACT_BASE + ACT_SIZE)

/* The monitor places each request here before running the verifier. */
#define MAILBOX_BASE    TDATA_BASE

/* CLINT: the machine timer (M-mode only). */
#define CLINT_BASE      0x02000000UL
#define CLINT_MTIMECMP  (CLINT_BASE + 0x4000)   /* hart 0 */
#define CLINT_MTIME     (CLINT_BASE + 0xBFF8)
#define TIMEBASE_HZ     10000000UL              /* mtime ticks per second on QEMU virt */

/* QEMU's "test" device: writing these values powers off or resets the machine. */
#define SYSCON_BASE     0x00100000UL
#define SYSCON_POWEROFF 0x5555
#define SYSCON_REBOOT   0x7777

#ifndef PAGE_SIZE               /* the Linux kernel has its own definition */
#define PAGE_SIZE       4096UL
#endif

#endif
