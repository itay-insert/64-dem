#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${BIOS_IMAGE:-$ROOT_DIR/bios-disk.img}"
IMAGE_SIZE_MIB="${BIOS_IMAGE_SIZE_MIB:-128}"
PARTITION_START=2048
SECTOR_SIZE=512
RUN_QEMU=true

usage() {
    printf 'Usage: %s [--no-run] [--image FILE] [--size MIB] [-- qemu-options...]\n' "$0"
}

EXTRA_QEMU_ARGS=()
while (($#)); do
    case "$1" in
        --no-run)
            RUN_QEMU=false
            shift
            ;;
        --image)
            [[ $# -ge 2 ]] || { echo "error: --image requires a path" >&2; exit 2; }
            IMAGE="$2"
            shift 2
            ;;
        --size)
            [[ $# -ge 2 ]] || { echo "error: --size requires a size in MiB" >&2; exit 2; }
            IMAGE_SIZE_MIB="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            EXTRA_QEMU_ARGS=("$@")
            break
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

for tool in nasm sfdisk mkfs.fat mcopy truncate dd; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: required tool '$tool' was not found" >&2
        exit 1
    }
done

if [[ ! "$IMAGE_SIZE_MIB" =~ ^[0-9]+$ ]] || ((IMAGE_SIZE_MIB < 64)); then
    echo "error: image size must be an integer of at least 64 MiB" >&2
    exit 2
fi

if [[ "$RUN_QEMU" == true ]] && ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "error: required tool 'qemu-system-x86_64' was not found" >&2
    exit 1
fi

BUILD_DIR="$(mktemp -d)"
cleanup() {
    rm -rf -- "$BUILD_DIR"
}
trap cleanup EXIT INT TERM

echo "Building UEFI loader and kernel..."
(cd "$ROOT_DIR" && bash ./make.sh)

echo "Assembling BIOS boot sector..."
nasm -f bin "$ROOT_DIR/boot/bios/boot.asm" -o "$BUILD_DIR/boot.bin"

BOOT_SIZE="$(stat -c '%s' "$BUILD_DIR/boot.bin")"
if ((BOOT_SIZE > 440)); then
    echo "error: BIOS MBR loader is $BOOT_SIZE bytes; at most 440 bytes are available" >&2
    exit 1
fi

TOTAL_SECTORS=$((IMAGE_SIZE_MIB * 1024 * 1024 / SECTOR_SIZE))
PARTITION_SECTORS=$((TOTAL_SECTORS - PARTITION_START))
PARTITION_OFFSET=$((PARTITION_START * SECTOR_SIZE))
FAT_SIZE_KIB=$((PARTITION_SECTORS / 2))

echo "Creating ${IMAGE_SIZE_MIB} MiB MBR disk image at $IMAGE..."
truncate -s "${IMAGE_SIZE_MIB}M" "$IMAGE"
sfdisk --wipe always "$IMAGE" <<EOF
label: dos
unit: sectors

start=$PARTITION_START, size=$PARTITION_SECTORS, type=c, bootable
EOF

# Preserve the partition table at bytes 446-509 and the 0x55AA MBR signature.
dd if="$BUILD_DIR/boot.bin" of="$IMAGE" bs=1 count="$BOOT_SIZE" conv=notrunc status=none

echo "Formatting the partition as FAT32..."
mkfs.fat -F 32 --offset "$PARTITION_START" -n 64DEM "$IMAGE" "$FAT_SIZE_KIB"

echo "Copying boot/ and EFI/ into the FAT32 filesystem..."
mcopy -i "$IMAGE@@$PARTITION_OFFSET" -s "$ROOT_DIR/boot" "$ROOT_DIR/EFI" ::/

echo "BIOS image ready: $IMAGE"

if [[ "$RUN_QEMU" == true ]]; then
    exec qemu-system-x86_64 \
        -machine pc \
        -m 512M \
        -drive "format=raw,file=$IMAGE" \
        -debugcon stdio \
        "${EXTRA_QEMU_ARGS[@]}"
fi
