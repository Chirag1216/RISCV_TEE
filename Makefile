CROSS   ?= riscv-none-elf-
CC      := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump
OBJCOPY := $(CROSS)objcopy

ARCH    := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
CFLAGS  := $(ARCH) -ffreestanding -fno-builtin -nostdlib -O2 -g -Wall -Wextra -Iinclude -MMD -MP
LDFLAGS := $(ARCH) -nostdlib -Wl,--no-relax -Wl,--no-warn-rwx-segments

BUILD   := build

# The monitor: the only code that runs in M-mode.
MON_SRC := monitor/start.S monitor/main.c monitor/uart.c monitor/trap.S monitor/trap.c monitor/pmp.c monitor/sbi.c monitor/enter.S monitor/timer.c monitor/world.c monitor/switch.S monitor/verifier_blob.S monitor/fdt.c common/string.c
MON_OBJ := $(patsubst %,$(BUILD)/%.o,$(MON_SRC))

# The verifier: trusted U-mode code. Its binary is embedded in the monitor.
# VERIFIER_TEST_HOOKS adds the "hang" and "fault" test commands: remove for a real build.
VER_SRC := verifier/entry.S verifier/verifier.c verifier/hmac_sha256.c common/string.c
VER_OBJ := $(patsubst %,$(BUILD)/ver/%.o,$(VER_SRC))
VER_CFLAGS := $(CFLAGS) -DVERIFIER_TEST_HOOKS

# The normal-world test payload: runs in S-mode where Linux will run later.
PAY_SRC := payload/start.S payload/main.c
PAY_OBJ := $(patsubst %,$(BUILD)/%.o,$(PAY_SRC))

QEMU      := qemu-system-riscv64
QEMU_BASE := -M virt -cpu rv64,sstc=true -smp 1 -m 2G -nographic -bios $(BUILD)/monitor.elf

# `make run`: the monitor with the S-mode test payload as the normal world.
QEMU_ARGS := $(QEMU_BASE) -kernel $(BUILD)/payload.elf

# `make linux`: the monitor with Linux as the normal world.
# Uses your own build from linux/build.sh if present, else the prebuilt files.
LINUX_DIR := $(if $(wildcard linux/out/Image),linux/out,linux/prebuilt)
LINUX_ARGS := $(QEMU_BASE) -kernel $(LINUX_DIR)/Image -initrd $(LINUX_DIR)/initramfs.cpio.gz \
              -append "console=ttyS0 earlycon=sbi rdinit=/init"

all: $(BUILD)/monitor.elf $(BUILD)/payload.elf

$(BUILD)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ver/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(VER_CFLAGS) -c $< -o $@

$(BUILD)/ver/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(VER_CFLAGS) -c $< -o $@

# Verifier: link, then strip to a raw binary for embedding.
$(BUILD)/verifier.elf: $(VER_OBJ) verifier/verifier.ld
	$(CC) $(LDFLAGS) -T verifier/verifier.ld $(VER_OBJ) -o $@ -lgcc
	$(OBJDUMP) -d $@ > $(BUILD)/verifier.dis

$(BUILD)/verifier.bin: $(BUILD)/verifier.elf
	$(OBJCOPY) -O binary $< $@

# The blob file includes verifier.bin, so it must be rebuilt when the verifier changes.
$(BUILD)/monitor/verifier_blob.S.o: $(BUILD)/verifier.bin

$(BUILD)/monitor.elf: $(MON_OBJ) monitor/monitor.ld
	$(CC) $(LDFLAGS) -T monitor/monitor.ld $(MON_OBJ) -o $@ -lgcc
	$(OBJDUMP) -d $@ > $(BUILD)/monitor.dis

$(BUILD)/payload.elf: $(PAY_OBJ) payload/payload.ld
	$(CC) $(LDFLAGS) -T payload/payload.ld $(PAY_OBJ) -o $@ -lgcc
	$(OBJDUMP) -d $@ > $(BUILD)/payload.dis

# Header dependencies written by -MMD, so editing a .h (e.g. policy.h) rebuilds what uses it.
-include $(MON_OBJ:.o=.d) $(VER_OBJ:.o=.d) $(PAY_OBJ:.o=.d)

run: all
	$(QEMU) $(QEMU_ARGS)

linux: $(BUILD)/monitor.elf
	$(QEMU) $(LINUX_ARGS)

# `make replay CMD=data/x.cmd`: boot Linux, play the command file through the
# verifier, power off. Console output goes to results/x.log for tools/score.py.
# The command file rides in a second initramfs appended to the first.
replay: $(BUILD)/monitor.elf
	@test -n "$(CMD)" || { echo "usage: make replay CMD=data/<name>.cmd"; exit 1; }
	@rm -rf $(BUILD)/replay && mkdir -p $(BUILD)/replay/data results
	@cp $(CMD) $(BUILD)/replay/data/replay.cmd
	@(cd $(BUILD)/replay && find . | cpio -o -H newc 2>/dev/null | gzip) > $(BUILD)/replay.cpio.gz
	@cat $(LINUX_DIR)/initramfs.cpio.gz $(BUILD)/replay.cpio.gz > $(BUILD)/initramfs-replay.cpio.gz
	$(QEMU) $(QEMU_BASE) -kernel $(LINUX_DIR)/Image -initrd $(BUILD)/initramfs-replay.cpio.gz \
	    -append "console=ttyS0 loglevel=3 rdinit=/init rvtee.replay=/data/replay.cmd" \
	    < /dev/null | tee results/$(notdir $(basename $(CMD))).log

debug: all
	$(QEMU) $(QEMU_ARGS) -s -S

gdb:
	$(CROSS)gdb $(BUILD)/monitor.elf -ex "target remote :1234"

clean:
	rm -rf $(BUILD)

.PHONY: all run linux replay debug gdb clean
