#!/usr/bin/env bash
set -e

VARIANT="${VARIANT:-e}"
SHA="$(git rev-parse --short HEAD)"
NAME="langsdorff-a12s-${SHA}-$([ "$VARIANT" = p ] && echo permissive || echo enforcing)"

KB="$(pwd)/kernel_build"
OUTDIR="$(pwd)/out"
TMPDIR="$KB/tmp"
RAMDISK_DIR="$TMPDIR/ramdisk"
AVB_KEY="${AVB_KEY:-$KB/mkbootimg/gki/testdata/testkey_rsa4096.pem}"

# A12s boot partition, from rsuntk/samsung_kernel_action a12s-5.10
BOOT_PART_SIZE=46137344
DTBO_PART_SIZE=8388608

FRAG=
CMDLINE="androidboot.hardware=s5e3830 androidboot.selinux=enforce loop.max_part=7"
if [ "$VARIANT" = p ]; then
    FRAG=permissive.config
    CMDLINE="androidboot.hardware=s5e3830 loop.max_part=7"
fi

export ARCH=arm64 LLVM=1 LLVM_IAS=1 DTC_FLAGS="-@"
export CROSS_COMPILE=aarch64-linux-gnu-
ARGS=(O=out LOCALVERSION=-langsdorff-a12s-${SHA}${VARIANT} KBUILD_BUILD_USER=Langsdorff KBUILD_BUILD_HOST=langsdorff)

make "${ARGS[@]}" exynos850-a12snsxx_defconfig $FRAG
make -j$(nproc --all) "${ARGS[@]}" Image.gz dtbs

rm -rf "$TMPDIR"
mkdir -p "$RAMDISK_DIR"
cp -a "$KB/a12s/ramdisk/." "$RAMDISK_DIR/"
for d in debug_ramdisk dev metadata mnt proc second_stage_resources sys; do
    mkdir -p "$RAMDISK_DIR/$d"
done
chmod 750 "$RAMDISK_DIR/init"
chmod 640 "$RAMDISK_DIR/fstab.s5e3830"
chmod 644 "$RAMDISK_DIR/dpolicy"
(cd "$RAMDISK_DIR" && find . | sort | cpio --quiet -o -H newc -R root:root | gzip -9 -n -c > "$TMPDIR/ramdisk.cpio")

python3 "$KB/mkdtboimg.py" create "$TMPDIR/dtb.img" --page_size=2048 --version=0 \
    "$OUTDIR/arch/arm64/boot/dts/exynos/s5e3830.dtb" --id=0 --rev=0 --custom0=0x0 --custom1=0xff
python3 "$KB/mkdtboimg.py" create "$TMPDIR/dtbo.img" --page_size=2048 --version=0 \
    "$OUTDIR/arch/arm64/boot/dts/samsung/a12s/a12s_eur_open_w00_r00.dtbo" --id=0 --rev=0 --custom0=0x0 --custom1=0x0 \
    "$OUTDIR/arch/arm64/boot/dts/samsung/a12s/a12s_eur_open_w00_r01.dtbo" --id=0 --rev=0 --custom0=0x1 --custom1=0x20

python3 "$KB/mkbootimg/mkbootimg.py" --header_version 2 \
    --kernel "$OUTDIR/arch/arm64/boot/Image.gz" \
    --ramdisk "$TMPDIR/ramdisk.cpio" \
    --dtb "$TMPDIR/dtb.img" \
    --base 0x10000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 \
    --tags_offset 0x00000100 --dtb_offset 0x00000000 --pagesize 2048 \
    --board SRPUE06B013 \
    --cmdline "$CMDLINE" \
    --os_version 13.0.0 --os_patch_level 2024-10 \
    --output "$TMPDIR/boot.img"

BOOT_SIZE=$(stat -c %s "$TMPDIR/boot.img")
echo "boot.img: $BOOT_SIZE / $BOOT_PART_SIZE"
if [ "$BOOT_SIZE" -gt "$BOOT_PART_SIZE" ]; then
    echo "ERROR: boot.img does not fit the A12s boot partition"
    exit 1
fi

python3 "$KB/avbtool" add_hash_footer --image "$TMPDIR/boot.img" --partition_name boot \
    --partition_size $BOOT_PART_SIZE --algorithm SHA256_RSA4096 --key "$AVB_KEY"
python3 "$KB/avbtool" add_hash_footer --image "$TMPDIR/dtbo.img" --partition_name dtbo \
    --partition_size $DTBO_PART_SIZE --algorithm SHA256_RSA4096 --key "$AVB_KEY"

(cd "$TMPDIR" && for f in boot.img dtbo.img; do lz4 -c -12 -B6 --content-size "$f" > "$f.lz4"; done \
    && tar -cf "$KB/$NAME.tar" boot.img.lz4 dtbo.img.lz4)
cp -f "$TMPDIR/boot.img" "$KB/$NAME-boot.img"
echo "Done: $KB/$NAME.tar"
