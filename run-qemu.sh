#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
QEMU_BIN="${QEMU:-qemu-system-x86_64}"
FIRMWARE="${OVMF_CODE:-}"
VARS_TEMPLATE="${OVMF_VARS:-}"
MEMORY="512M"
VGA_MEMORY="2"
CPU_MODEL="${QEMU_CPU:-}"
BUILD=true
HEADLESS=false

usage() {
    cat <<'EOF'
Usage: ./run-qemu.sh [options] [-- qemu-options...]

Build and run 64-dem in QEMU using x86-64 UEFI firmware.

Options:
  --no-build           Run the existing binaries without rebuilding them
  --memory SIZE        Guest RAM size (default: 512M)
  --vga-memory MB      VGA memory in MB (default: 2)
  --cpu MODEL          Override QEMU's default CPU model
  --firmware FILE      Path to OVMF_CODE firmware
  --headless           Disable the graphical GOP framebuffer window
  -h, --help           Show this help

Environment variables:
  QEMU                 QEMU executable (default: qemu-system-x86_64)
  QEMU_CPU             QEMU CPU model (same as --cpu)
  OVMF_CODE            Path to the OVMF code image
  OVMF_VARS            Optional path to the OVMF variables template

Any arguments after -- are passed directly to QEMU.
EOF
}

EXTRA_QEMU_ARGS=()
while (($#)); do
    case "$1" in
        --no-build)
            BUILD=false
            shift
            ;;
        --memory)
            if (($# < 2)); then
                echo "error: --memory requires a value" >&2
                exit 2
            fi
            MEMORY="$2"
            shift 2
            ;;
        --vga-memory)
            if (($# < 2)); then
                echo "error: --vga-memory requires a value" >&2
                exit 2
            fi
            VGA_MEMORY="$2"
            shift 2
            ;;
        --cpu)
            if (($# < 2)); then
                echo "error: --cpu requires a value" >&2
                exit 2
            fi
            CPU_MODEL="$2"
            shift 2
            ;;
        --firmware)
            if (($# < 2)); then
                echo "error: --firmware requires a file" >&2
                exit 2
            fi
            FIRMWARE="$2"
            shift 2
            ;;
        --headless)
            HEADLESS=true
            shift
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
            echo "Try './run-qemu.sh --help' for usage." >&2
            exit 2
            ;;
    esac
done

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "error: '$QEMU_BIN' was not found; install qemu-system-x86." >&2
    exit 1
fi

if [[ -z "$FIRMWARE" ]]; then
    firmware_candidates=(
        /usr/share/OVMF/OVMF_CODE_4M.fd
        /usr/share/OVMF/OVMF_CODE.fd
        /usr/share/edk2/ovmf/OVMF_CODE.fd
        /usr/share/edk2/x64/OVMF_CODE.fd
        /usr/share/qemu/OVMF_CODE.fd
    )
    for candidate in "${firmware_candidates[@]}"; do
        if [[ -r "$candidate" ]]; then
            FIRMWARE="$candidate"
            break
        fi
    done
fi

if [[ -z "$FIRMWARE" || ! -r "$FIRMWARE" ]]; then
    echo "error: OVMF x86-64 firmware was not found." >&2
    echo "Install the 'ovmf' package or set OVMF_CODE to its path." >&2
    exit 1
fi

if [[ "$BUILD" == true ]]; then
    echo "Building 64-dem..."
    (cd "$ROOT_DIR" && bash ./make.sh)
fi

for required_file in EFI/boot/BOOTX64.efi boot/kernel.elf; do
    if [[ ! -r "$ROOT_DIR/$required_file" ]]; then
        echo "error: missing $required_file; run without --no-build first." >&2
        exit 1
    fi
done

if [[ -z "$VARS_TEMPLATE" ]]; then
    case "$FIRMWARE" in
        */OVMF_CODE_4M.fd)
            candidate="${FIRMWARE%/*}/OVMF_VARS_4M.fd"
            [[ -r "$candidate" ]] && VARS_TEMPLATE="$candidate"
            ;;
        */OVMF_CODE.fd)
            candidate="${FIRMWARE%/*}/OVMF_VARS.fd"
            [[ -r "$candidate" ]] && VARS_TEMPLATE="$candidate"
            ;;
    esac
fi

if [[ -r /dev/kvm && -w /dev/kvm ]]; then
    ACCELERATOR=kvm
else
    ACCELERATOR=tcg
fi

QEMU_ARGS=(
    -machine pc
    -accel "$ACCELERATOR"
    -m "$MEMORY"
    -global "VGA.vgamem_mb=$VGA_MEMORY"
    -vga std
    -drive "if=pflash,format=raw,readonly=on,file=$FIRMWARE"
)

if [[ -n "$CPU_MODEL" ]]; then
    QEMU_ARGS+=(-cpu "$CPU_MODEL")
fi

TEMP_DIR="$(mktemp -d)"
cleanup() {
    rm -rf -- "$TEMP_DIR"
}
trap cleanup EXIT INT TERM

if [[ -n "$VARS_TEMPLATE" ]]; then
    if [[ ! -r "$VARS_TEMPLATE" ]]; then
        echo "error: OVMF_VARS is not readable: $VARS_TEMPLATE" >&2
        exit 1
    fi
    cp -- "$VARS_TEMPLATE" "$TEMP_DIR/OVMF_VARS.fd"
    QEMU_ARGS+=(
        -drive "if=pflash,format=raw,file=$TEMP_DIR/OVMF_VARS.fd"
    )
fi

# Build a disposable FAT disk tree so firmware writes cannot alter the source
# directory. It contains the standard x86-64 UEFI fallback boot path.
mkdir -p "$TEMP_DIR/disk/EFI/boot" "$TEMP_DIR/disk/boot"
cp -- "$ROOT_DIR/EFI/boot/BOOTX64.efi" "$TEMP_DIR/disk/EFI/boot/BOOTX64.efi"
cp -- "$ROOT_DIR/boot/kernel.elf" "$TEMP_DIR/disk/boot/kernel.elf"

QEMU_ARGS+=(
    -drive "format=raw,media=disk,file=fat:rw:$TEMP_DIR/disk"
    -monitor none
    -serial none
    -debugcon stdio
    -global isa-debugcon.iobase=0xe9
    -no-reboot
)

# Port 0xE9 output is connected to this terminal while QEMU renders the GOP
# framebuffer in a separate window. --headless disables only that window.
DISPLAY_CONFIGURED=false
for arg in "${EXTRA_QEMU_ARGS[@]}"; do
    case "$arg" in
        -display|-display=*|-nographic|-vnc|-vnc=*) DISPLAY_CONFIGURED=true ;;
    esac
done
if [[ "$DISPLAY_CONFIGURED" == false && "$HEADLESS" == true ]]; then
    QEMU_ARGS+=(-display none)
elif [[ "$DISPLAY_CONFIGURED" == false ]]; then
    QEMU_ARGS+=(-display gtk)
fi
QEMU_ARGS+=("${EXTRA_QEMU_ARGS[@]}")

echo "Starting 64-dem with $QEMU_BIN; kernel debug output follows (Ctrl-C to stop)."
TMPDIR="$TEMP_DIR" "$QEMU_BIN" "${QEMU_ARGS[@]}"
