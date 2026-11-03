#!/bin/bash
# usage: sudo SEPATCH=<sepatch> mkodm_a12s.sh <a12s stock vendor files> <A14 stock odm.img (erofs)> <out.img>
set -euo pipefail

[ $# -eq 3 ] || { sed -n 2p "$0" >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo "run as root (xattrs)" >&2; exit 1; }
for t in fsck.erofs mkfs.erofs dump.erofs setfattr "${SEPATCH:?SEPATCH is not set}"; do
    command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done

A12S=$(realpath "$1")/odm
KIT=$(dirname "$(realpath "$0")")
A14_IMG=$(realpath "$2")
OUT_IMG=$(realpath -m "$3")

WORK=$(mktemp -d /var/tmp/mkodm_a12s.XXXX)
TREE=$WORK/odm
trap 'rm -rf "$WORK"' EXIT

fsck.erofs --extract="$TREE" --xattrs --preserve --overwrite "$A14_IMG"

# precompiled_sepolicy stays A14 so it matches the A14 vendor policy, plus the a12s_klog rules.
# The sha256 files hash the plat sources, so init still takes the patched binary.
sp=$TREE/etc/selinux/precompiled_sepolicy
"$SEPATCH" "$sp" "$WORK/sepolicy" < "$KIT/a12s_klog.te"
cat "$WORK/sepolicy" > "$sp"

# The NFC SKU manifests declare the HAL: A14 has the Samsung AIDL S3NRN4V one, a12s the NXP HIDL nfc@1.2 one.
(cd "$A12S" && find etc/vintf etc/permissions -type f) | while read -r f; do
    if [ -f "$TREE/$f" ]; then
        cat "$A12S/$f" > "$TREE/$f"
    else
        install -D -m 644 -o 0 -g 0 "$A12S/$f" "$TREE/$f"
        setfattr -n security.selinux -v u:object_r:vendor_configs_file:s0 "$TREE/$f"
    fi
done
grep -q '<fqname>@1.2::INfc/default</fqname>' "$TREE/etc/vintf/manifest_hce.xml"

rm -f "$OUT_IMG"
UUID=$(dump.erofs -s "$A14_IMG" | awk '/Filesystem UUID/ {print $NF}')
mkfs.erofs -zlz4hc,9 -T1230768000 -U "$UUID" -b4096 -Lodm -E^xattr-name-filter --quiet "$OUT_IMG" "$TREE"
fsck.erofs "$OUT_IMG"
ls -l "$OUT_IMG"
sha1sum "$OUT_IMG"
