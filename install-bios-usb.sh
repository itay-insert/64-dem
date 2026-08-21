#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET=""
ASSUME_YES=false
ALLOW_NON_REMOVABLE=false

usage() {
    cat <<EOF
Usage: $0 [--yes] [--allow-non-removable] /dev/DEVICE

Build 64-dem, partition a USB drive, and install BIOS/UEFI boot files.

Options:
  --yes                    Skip the typed-device confirmation
  --allow-non-removable    Permit a device not marked removable by Linux
  -h, --help               Show this help
EOF
}

while (($#)); do
    case "$1" in
        --yes)
            ASSUME_YES=true
            shift
            ;;
        --allow-non-removable)
            ALLOW_NON_REMOVABLE=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        /dev/*)
            [[ -z "$TARGET" ]] || { echo "error: specify exactly one target device" >&2; exit 2; }
            TARGET="$1"
            shift
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[[ -n "$TARGET" ]] || { usage >&2; exit 2; }

for tool in lsblk findmnt readlink nasm sudo dd sync awk stat mcopy udevadm; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: required tool '$tool' was not found" >&2
        exit 1
    }
done

find_admin_tool() {
    local name="$1"
    local candidate

    candidate="$(command -v "$name" 2>/dev/null || true)"
    if [[ -n "$candidate" ]]; then
        printf '%s\n' "$candidate"
    elif [[ -x "/usr/sbin/$name" ]]; then
        printf '/usr/sbin/%s\n' "$name"
    elif [[ -x "/sbin/$name" ]]; then
        printf '/sbin/%s\n' "$name"
    else
        echo "error: required tool '$name' was not found" >&2
        return 1
    fi
}

SFDISK="$(find_admin_tool sfdisk)"
MKFS_FAT="$(find_admin_tool mkfs.fat)"
WIPEFS="$(find_admin_tool wipefs)"

TARGET="$(readlink -f -- "$TARGET")"
[[ -b "$TARGET" ]] || { echo "error: not a block device: $TARGET" >&2; exit 1; }

TARGET_TYPE="$(lsblk -dnro TYPE -- "$TARGET")"
[[ "$TARGET_TYPE" == disk ]] || {
    echo "error: target must be a whole disk, not a partition: $TARGET" >&2
    exit 1
}

TARGET_NAME="${TARGET##*/}"
REMOVABLE="$(lsblk -dnro RM -- "$TARGET")"
if [[ "$REMOVABLE" != 1 && "$ALLOW_NON_REMOVABLE" != true ]]; then
    echo "error: $TARGET is not marked removable" >&2
    echo "Use --allow-non-removable only if you have independently verified the device." >&2
    exit 1
fi

ROOT_SOURCE="$(findmnt -nro SOURCE /)"
while IFS= read -r ROOT_ANCESTOR; do
    if [[ "$TARGET_NAME" == "$ROOT_ANCESTOR" ]]; then
        echo "error: refusing to overwrite the disk containing the running root filesystem" >&2
        exit 1
    fi
done < <(lsblk -snro NAME -- "$ROOT_SOURCE" 2>/dev/null || true)

MOUNTED="$(lsblk -nrpo NAME,MOUNTPOINTS -- "$TARGET" | awk 'NF > 1 { print }')"
if [[ -n "$MOUNTED" ]]; then
    echo "error: target or one of its partitions is mounted:" >&2
    printf '%s\n' "$MOUNTED" >&2
    echo "Unmount it before running this installer." >&2
    exit 1
fi

echo "Target device:"
lsblk -dno NAME,TRAN,RM,SIZE,VENDOR,MODEL,SERIAL -- "$TARGET"
echo
echo "WARNING: ALL DATA ON $TARGET WILL BE DESTROYED."

if [[ "$ASSUME_YES" != true ]]; then
    read -r -p "Type the full device path '$TARGET' to continue: " CONFIRMATION
    [[ "$CONFIRMATION" == "$TARGET" ]] || {
        echo "Cancelled; nothing was written."
        exit 1
    }
fi

BUILD_DIR="$(mktemp -d)"
cleanup() {
    rm -rf -- "$BUILD_DIR"
}
trap cleanup EXIT INT TERM

echo "Assembling stage 2..."
nasm -f bin "$ROOT_DIR/boot/bios/stage2.asm" -o "$ROOT_DIR/boot/stage2.bin"
STAGE2_SIZE="$(stat -c '%s' "$ROOT_DIR/boot/stage2.bin")"
if ((STAGE2_SIZE > 512)); then
    echo "error: stage2.bin is $STAGE2_SIZE bytes; stage 1 loads only one 512-byte sector" >&2
    exit 1
fi

echo "Building kernel and UEFI loader..."
(cd "$ROOT_DIR" && bash ./make.sh)

echo "Assembling BIOS MBR loader..."
nasm -f bin "$ROOT_DIR/boot/bios/boot.asm" -o "$BUILD_DIR/boot.bin"
BOOT_SIZE="$(stat -c '%s' "$BUILD_DIR/boot.bin")"
if ((BOOT_SIZE > 440)); then
    echo "error: BIOS MBR loader is $BOOT_SIZE bytes; at most 440 bytes are available" >&2
    exit 1
fi

echo "Creating an MBR partition table on $TARGET..."
sudo "$WIPEFS" --all --force "$TARGET"
sudo "$SFDISK" --wipe always --wipe-partitions always "$TARGET" <<EOF
label: dos
unit: sectors

start=2048, type=c, bootable
EOF

sudo udevadm settle
PARTITION="$(lsblk -nrpo NAME,TYPE -- "$TARGET" | awk '$2 == "part" { print $1; exit }')"
if [[ -z "$PARTITION" || ! -b "$PARTITION" ]]; then
    echo "error: the new USB partition did not appear" >&2
    exit 1
fi

echo "Formatting $PARTITION as FAT32..."
sudo "$MKFS_FAT" -F 32 -n 64DEM "$PARTITION"

echo "Installing the BIOS loader into the MBR..."
sudo dd if="$BUILD_DIR/boot.bin" of="$TARGET" bs=1 count="$BOOT_SIZE" conv=notrunc,fsync status=none

echo "Copying boot/ and EFI/ to the USB filesystem..."
sudo mcopy -i "$PARTITION" -s "$ROOT_DIR/boot" "$ROOT_DIR/EFI" ::/
sudo sync

echo "Installation complete. $PARTITION uses the available USB capacity."
echo "Safely remove the drive before unplugging it."
