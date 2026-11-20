#!/usr/bin/env bash
set -euo pipefail

shell_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vim_src=$shell_dir/vim/src
cc=${CC:-gcc}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
build=$(mktemp -d "$shell_dir/.vim-build.XXXXXX")
trap 'rm -rf -- "$build"' EXIT

cp -a -- "$vim_src" "$build/src"
cd -- "$build/src"
# Configure the host headers, then select the facilities 64-dem implements.
if ! CC="$cc" ./configure --with-features=tiny --disable-gui --without-x \
    --disable-nls --disable-acl --disable-selinux --disable-xsmp \
    --disable-channel --disable-terminal --disable-netbeans --disable-gpm \
    > "$build/configure.log" 2>&1; then
    cat "$build/configure.log" >&2
    exit 1
fi
sed -i -E 's/^#define (TERMINFO|HAVE_(TGETENT|FTRUNCATE|FCHOWN|FCHMOD|FLOCK|FSYNC|ICONV|LSTAT|NL_LANGINFO_CODESET|GETRLIMIT|GETTIMEOFDAY|NANOSLEEP|STRFTIME|SIGACTION|SIGALTSTACK|SIGSTACK|SIGSET|GETPWNAM|GETPWUID|RENAME)) .*/#undef \1/' auto/config.h

flags=(-O2 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone
       -ffunction-sections -fdata-sections -U_FORTIFY_SOURCE -DVIM_64DEM)
cat > "$build/objects.mk" <<'MAKEFILE'
.PHONY: dem-objects
dem-objects: $(OBJ) objects/version.o
	@printf '%s\n' $(OBJ) > ../vim-objects
MAKEFILE
if ! make -j "$jobs" -f Makefile -f "$build/objects.mk" CC="$cc" \
    CFLAGS="${flags[*]}" dem-objects > "$build/compile.log" 2>&1; then
    tail -80 "$build/compile.log" >&2
    exit 1
fi
mapfile -t vim_objects < "$build/vim-objects"

nasm -f elf64 "$shell_dir/lib/sys.asm" -o "$build/sys.o"
objcopy --redefine-sym malloc=dem_alloc --redefine-sym free=dem_free \
    --redefine-sym realloc=dem_realloc "$build/sys.o"
nasm -f elf64 "$shell_dir/lib/vim_entry.asm" -o "$build/entry.o"
"$cc" "${flags[@]}" -I"$shell_dir/../boot/kernel/include/drivers/keyboard" -c os_64dem.c -o "$build/port.o"
"$cc" "${flags[@]}" -c "$shell_dir/lib/vim_runtime.c" -o "$build/runtime.o"
"$cc" -nostdlib -static -no-pie -Wl,--gc-sections,--build-id=none,--no-warn-rwx-segments,-z,noexecstack \
    -Wl,-T,"$shell_dir/lib/vim_link.ld" -o "$build/vim.elf" \
    "$build/entry.o" "$build/sys.o" "$build/port.o" "$build/runtime.o" \
    "${vim_objects[@]}" -lgcc
mv -- "$build/vim.elf" "$shell_dir/vim.elf"
printf 'Built %s\n' "$shell_dir/vim.elf"
