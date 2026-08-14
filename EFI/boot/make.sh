BOOTLOADER_OUTPUT="${BOOTLOADER_OUTPUT:-BOOTX64.efi}"
DEBUG_DEFINE=()
if [[ "${VISUAL_DEBUGGER:-0}" == 1 ]]; then
  DEBUG_DEFINE=(-DVISUAL_DEBUGGER)
fi

gcc -march=x86-64 \
  -I/usr/include/efi \
  -I/usr/include/efi/x86_64 \
  -ffreestanding -fshort-wchar -mno-red-zone \
  "${DEBUG_DEFINE[@]}" \
  -c bootloader.c -o bootloader.o


ld \
  -nostdlib -znocombreloc \
  -T /usr/lib/elf_x86_64_efi.lds \
  -shared -Bsymbolic \
  -L/usr/lib \
  /usr/lib/crt0-efi-x86_64.o \
  bootloader.o \
  -lefi -lgnuefi \
  -o bootloader.so

objcopy \
  -j .text -j .rodata -j .sdata -j .data -j .bss \
  -j .rel -j .rela -j .reloc \
  --target efi-app-x86_64 \
  bootloader.so "$BOOTLOADER_OUTPUT"

if [[ -n "${BOOTLOADER_DEBUG_ELF:-}" ]]; then
  cp -- bootloader.so "$BOOTLOADER_DEBUG_ELF"
fi

  rm *.o
  rm *.so
