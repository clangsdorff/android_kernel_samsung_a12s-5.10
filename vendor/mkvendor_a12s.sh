#!/bin/bash
# usage: sudo CC=<ndk clang> mkvendor_a12s.sh <a12s stock vendor files> <A14 stock vendor.img (erofs)> <out.img>
set -euo pipefail

[ $# -eq 3 ] || { sed -n 2p "$0" >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo "run as root (xattrs)" >&2; exit 1; }
for t in fsck.erofs mkfs.erofs dump.erofs setfattr getfattr readelf patchelf python3 "${CC:?CC is not set}"; do
    command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done

A12S=$(realpath "$1")
A14_IMG=$(realpath "$2")
OUT_IMG=$(realpath -m "$3")
KIT=$(dirname "$(realpath "$0")")

WORK=$(mktemp -d /var/tmp/mkvendor_a12s.XXXX)
TREE=$WORK/vendor
trap 'rm -rf "$WORK"' EXIT

label() { setfattr -n security.selinux -v "u:object_r:$1:s0" "$2"; }

fsck.erofs --extract="$TREE" --xattrs --preserve --overwrite "$A14_IMG"

a12s() { install -D -m "$2" -o 0 -g "$3" "$A12S/$1" "$TREE/$1"; label "$4" "$TREE/$1"; }
# replaces an A14 file in place; cat keeps the inode label
a12s_over() { [ -f "$TREE/$2" ]; cat "$A12S/$1" > "$TREE/$2"; }
a12shal() {
    local exec_label=$1 libs; shift
    libs=$(python3 "$KIT/a12shal.py" "$A12S" "$TREE" "$@")
    a12s "$1" 755 2000 "$exec_label"
    for l in $libs; do a12s "lib64/$l" 644 0 vendor_file; done
}

# Every driver is built into the kernel
rm -f "$TREE"/lib/modules/*.ko

# Defex policy and the stock recovery restoration
rm -rf "$TREE/etc/dpolicy" \
       "$TREE/recovery-from-boot.p" \
       "$TREE/bin/install-recovery.sh" \
       "$TREE/etc/init/vendor_flash_recovery.rc"

# a12s partition layout: no vbmeta_system, vendor_dlkm, system_dlkm, init_boot or vendor_boot
cat "$KIT/fstab.s5e3830" > "$TREE/etc/fstab.s5e3830"

# A14 KeyMint TA links EVP_PKEY_get_raw_private_key, which a12s TEEGRIS lacks
a12s_over tee/00000000-0000-0000-0000-4b45594d5354 tee/00000000-0000-0000-0000-4b45594d5354

# a12s Goodix side key: the a12s HIDL fingerprint@3.0 HAL and its TA replace the A14 AIDL HAL
rm -f "$TREE/etc/vintf/manifest/vendor.samsung.hardware.biometrics.fingerprint-service.xml" \
      "$TREE/etc/init/vendor.samsung.hardware.biometrics.fingerprint-service.rc"
a12shal hal_fingerprint_default_exec bin/hw/vendor.samsung.hardware.biometrics.fingerprint@3.0-service lib64/hw/fingerprint.default.so
a12s lib64/hw/fingerprint.default.so 644 0 vendor_file
a12s etc/init/vendor.samsung.hardware.biometrics.fingerprint@3.0-service.rc 644 0 vendor_configs_file
a12s etc/vintf/manifest/vendor.samsung.hardware.biometrics.fingerprint@3.0-service.xml 644 0 vendor_configs_file
a12s_over tee/00000000-0000-0000-0000-46494e474502 tee/00000000-0000-0000-0000-46494e474502

# a12s NFC is NXP PN557 on the pn547 driver; the A14 Samsung S3NRN4V HAL is swapped for the a12s NXP HIDL HAL
rm -f "$TREE/etc/init/sec-nfc-service.rc" "$TREE/etc/vintf/manifest/sec-nfc-service.xml"
a12shal hal_nfc_default_exec bin/hw/nxp.android.hardware.nfc@1.2-service lib64/nfc_nci_nxpsn.so
a12s etc/init/nxp.android.hardware.nfc@1.2-service.rc 644 0 vendor_configs_file
a12s etc/vintf/manifest/nxp.android.hardware.nfc@1.2-service.xml 644 0 vendor_configs_file
a12s etc/libnfc-nxp.conf 644 0 vendor_configs_file
a12s etc/nfc/libnfc-nxp_RF.conf 644 0 vendor_configs_file
a12s firmware/nfc/libpn557_fw.so 644 0 vendor_fw_file

# A14 camera HAL only knows A14 sensors; the a12s HAL, its plugins, DDK, RTA and setfiles are taken whole.
# The A14 HWL loads the legacy module as camera.<ro.hardware>.
a12s_over lib64/hw/camera.exynos850.so lib64/hw/camera.s5e3830.so
for l in libexynoscamera3.so libexynoscamera_plugin.so libexynoscamera_plugin_utils.so libremosaic_wrapper.so \
         libsec2lsi_conversion.so libexynoscamera_tdnr_plugin.so libmfb.so; do
    a12s_over "lib64/$l" "lib64/$l"
done
a12s lib64/libremosaiclib_s5kgm2_cpu.so 644 0 vendor_file
for f in setfile_gm2.bin setfile_4ha.bin setfile_sr846.bin setfile_gc5035.bin setfile_gc02m1.bin setfile_gc02m1_macro.bin \
         tdnr_S5KGM2.json tdnr_S5K4HA.json tdnr_SR846.json tdnr_GC5035.json tdnr_GC02M1.json dual_cal_wide_sub.bin; do
    a12s "firmware/$f" 644 0 vendor_fw_file
done
for f in is_lib.bin is_rta.bin; do
    python3 "$KIT/ddk_rebase.py" "$A12S/firmware/$f" "$WORK/$f"
    cat "$WORK/$f" > "$TREE/firmware/$f"
done

"$CC" --target=aarch64-linux-android30 -shared -fPIC -O2 -Wall -Werror -Wl,-soname,libui_shim.so -Wl,-z,defs \
    -o "$TREE/lib64/libui_shim.so" "$KIT/ui_shim.c" "$TREE/lib64/libui.so"
chown 0:0 "$TREE/lib64/libui_shim.so"; chmod 644 "$TREE/lib64/libui_shim.so"
label vendor_file "$TREE/lib64/libui_shim.so"
cp "$TREE/lib64/libexynoscamera3.so" "$WORK/libexynoscamera3.so"
patchelf --add-needed libui_shim.so "$WORK/libexynoscamera3.so"
cat "$WORK/libexynoscamera3.so" > "$TREE/lib64/libexynoscamera3.so"

save() {
    cat "$TREE/tmp" > "$TREE/$1"
    rm "$TREE/tmp"
}
drop_line() {
    grep -qE "$2" "$TREE/$1"
    grep -vE "$2" "$TREE/$1" > "$TREE/tmp"
    save "$1"
}
# a block is a paragraph between blank lines, e.g. a whole service definition
drop_block() {
    grep -qF "$2" "$TREE/$1"
    awk -v RS= -v ORS='\n\n' -v text="$2" 'index($0, text) == 0' "$TREE/$1" > "$TREE/tmp"
    save "$1"
}
replace() {
    grep -qF "$2" "$TREE/$1"
    F="$2" R="$3" awk '{ while ((i = index($0, ENVIRON["F"])) > 0) $0 = substr($0, 1, i - 1) ENVIRON["R"] substr($0, i + length(ENVIRON["F"])); print }' \
        "$TREE/$1" > "$TREE/tmp"
    save "$1"
}

# a12s speaker amp is SMA1303 (mute switch), A14 has AW88230 (profile + enable switch);
# the kernel has no ABOX OEM NXP Mode control
mp=etc/mixer_paths.xml
drop_line $mp '<ctl name="aw_dev_0_prof" value="0" />'
replace $mp '<ctl name="SPEAKER Switch" value="0" />' '<ctl name="Speaker Mute Switch(1:muted_0:un)" value="1" />'
replace $mp '<ctl name="SPEAKER Switch" value="1" />' '<ctl name="Speaker Mute Switch(1:muted_0:un)" value="0" />'
drop_line $mp '<ctl name="ABOX OEM NXP Mode" value="1" />'
if grep -qE 'aw_dev|SPEAKER Switch' "$TREE/$mp"; then echo "$mp: AW88230 controls left" >&2; exit 1; fi

# Lines that only log errors on a12s: no persist or spu partition, no vendor_dlkm, /data is not mounted
# at "on init", the vendor is read-only and some binaries are missing (32-bit, goodix, dhcpcd, wpal).
rc=etc/init/init.s5e3830.rc
drop_line $rc '^ *exec u:r:vendor_modprobe:s0 -- /vendor/bin/modprobe -a -d /vendor_dlkm/lib/modules input_booster_lkm\.ko$'
drop_line $rc '^ *# Set permissions for persist partition$'
drop_line $rc '^ *mkdir /persist '
drop_line $rc '^ *symlink /data/app /factory$'
drop_line $rc '^# Directory for GPS$'
drop_line $rc '^ *rm /data/system/gps/'
drop_line $rc '^ *setprop ro.radio.noril no$'
for s in goodixd dhcpcd_wlan0 iprenew_wlan0 dhcpcd_p2p iprenew_p2p dhcpcd_bt-pan iprenew_bt-pan; do
    drop_block $rc "service $s "
done

drop_line etc/init/init.sensorhub.rc ' /spu/sensorhub'
drop_line etc/init/wifi.rc ' /vendor/etc/wifi/indoorchannel\.info$'
drop_line etc/init/wifi_slsi_850.rc '^ *start wpal$'
drop_block etc/init/wifi_slsi_850.rc 'service wpal '
drop_block etc/init/snap_utility.rc 'service snap_utility_32 '
drop_block etc/init/snap_utility.rc 'start snap_utility_32'

# /sys entry without an attribute name
drop_line etc/ueventd.rc '^/sys/bus/iio/devices/iio:device\* +0664 +system +radio$'

replace build.prop 'ro.vendor.nfc.feature.chipname=SLSI' "$(grep -m1 '^ro.vendor.nfc.feature.chipname=' "$A12S/build.prop")"
# vendor_init may not set these; the stock file also repeats some lines with the same value
drop_line build.prop '^net\.dns[12]='
drop_line build.prop '^persist\.demo\.hdmirotationlock='
awk '!/=/ || !seen[$0]++' "$TREE/build.prop" > "$TREE/tmp"
save build.prop

rm -f "$OUT_IMG"
# stock A14 UUID so the same inputs give the same image hash
UUID=$(dump.erofs -s "$A14_IMG" | awk '/Filesystem UUID/ {print $NF}')
mkfs.erofs -zlz4hc,9 -T1230768000 -U "$UUID" -b4096 -Lvendor -E^xattr-name-filter --quiet "$OUT_IMG" "$TREE"
fsck.erofs "$OUT_IMG"
ls -l "$OUT_IMG"
sha1sum "$OUT_IMG"
