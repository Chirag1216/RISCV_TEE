#!/bin/sh
# Build the normal-world guest. Results go to linux/out/.
#
#   1. Linux 6.12 kernel with PREEMPT_RT
#   2. the /dev/dtee driver (kernel module)
#   3. BusyBox (static)
#   4. the user programs (dtee_test, replayer)
#   5. the root filesystem (initramfs) holding 2, 3 and 4
#
# Needs a Linux-targeting RISC-V toolchain (not the bare-metal one used for
# the monitor), plus git, make, flex, bison, bc, cpio and gzip.
#
#   CROSS=riscv64-linux-gnu- ./linux/build.sh
set -e

CROSS="${CROSS:-riscv64-linux-gnu-}"
JOBS="${JOBS:-$(nproc)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
OUT="$HERE/out"
KMAKE="make ARCH=riscv CROSS_COMPILE=$CROSS"
mkdir -p "$SRC" "$OUT"

# ---- 1. Kernel ----
[ -d "$SRC/linux" ] || git clone --depth 1 --branch v6.12 https://github.com/torvalds/linux.git "$SRC/linux"
cd "$SRC/linux"
$KMAKE tinyconfig
./scripts/kconfig/merge_config.sh -m .config "$HERE/rvtee.config"
$KMAKE olddefconfig
grep -q '^CONFIG_PREEMPT_RT=y' .config || { echo "PREEMPT_RT was not enabled"; exit 1; }
$KMAKE -j"$JOBS" Image modules
cp arch/riscv/boot/Image "$OUT/Image"

# ---- 2. Driver (must be built against this exact kernel tree) ----
$KMAKE -C "$SRC/linux" M="$HERE/driver" modules
cp "$HERE/driver/dtee.ko" "$OUT/dtee.ko"

# ---- 3. BusyBox (static) ----
[ -d "$SRC/busybox" ] || git clone --depth 1 --branch 1_36_1 https://github.com/mirror/busybox.git "$SRC/busybox"
cd "$SRC/busybox"
if [ ! -f .config ]; then
    $KMAKE defconfig
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/; s/CONFIG_TC=y/# CONFIG_TC is not set/' .config
    yes "" | $KMAKE oldconfig >/dev/null
fi
$KMAKE -j"$JOBS"

# ---- 4. User programs ----
"${CROSS}gcc" -O2 -Wall -static -I"$HERE/../include" -I"$HERE/../verifier" "$HERE/user/dtee_test.c" -o "$OUT/dtee_test"
"${CROSS}gcc" -O2 -Wall -static -I"$HERE/../include" "$HERE/user/replayer.c"  -o "$OUT/replayer"

# ---- 5. Root filesystem ----
ROOT="$OUT/rootfs"
rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/lib" "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" "$ROOT/tmp" "$ROOT/etc"
cp "$SRC/busybox/busybox" "$ROOT/bin/busybox"
cp "$OUT/dtee_test" "$ROOT/bin/dtee_test"
cp "$OUT/replayer" "$ROOT/bin/replayer"
cp "$OUT/dtee.ko" "$ROOT/lib/dtee.ko"
cp "$HERE/init" "$ROOT/init"
chmod +x "$ROOT/init"
(cd "$ROOT" && find . | cpio -o -H newc 2>/dev/null | gzip > "$OUT/initramfs.cpio.gz")

echo
echo "Built: $OUT/Image and $OUT/initramfs.cpio.gz"
echo "Boot with: make linux"
