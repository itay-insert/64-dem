# My operating system
64-dem is a 64-bit UEFI operating system booting from a USB flash drive, made for the x86-64/amd64 architecture.

## License
64-dem is licensed under the GNU General Public License version 2 only (GPL-2.0-only). 

## The bootloader 
At boot the UEFI bootloader gets the memory map from UEFI using a UEFI call, it also sets up the GOP picking the highest resolution it can find, after the GOP is setup the bootloader uses UEFI calls to read the file stored in path "/boot/kernel.elf", it uses a custom elf parser to parse the elf file, at first it calculates the size of all the segments in pages then allocates memory using it's own custom memory allocator, after allocating enough memory it parses the elf file, copies the segments to the right memory regions, exits UEFI services, disables interrupts and jumps to the kernel entry passing some parameters along the way.


## The kernel entry
The kernel is given a few parameters from the UEFI bootloader, one of those paramaters is the GOP framebuffer base, and other important GOP data, and another important parameter is a pointer to the UEFI memory map, the kernel includes it's own "vga subsystem" to use the GOP even after UEFI boot services is closed, at first it checks a global variable called "paging enabled" if it's 1 the kernel skips the paging setup and moves on to initialize other stuff, if it's 0 the kernel sets up it's own page tables first before moving on, the paging flag is automatically set to 0 by the bootloader when loading the kernel.

## Paging
when "paging enabled" is 0, the kernel takes the "Paging route" in which it sets up paging, to begin the kernel passes the UEFI memory map to it's own function called "allocator_init()" which basically sets up a simple bitmap allocator for the kernel to use, the size of the allocator as well as how much memory it uses was all computed inside the bootloader and passed to the kernel, the allocator_init() reads the UEFI memory map and marks free regions as 0, used regions as 1, then it masks the kernel regions as used too, after initializing the allocator, the paging route uses a function called create_mapping() to identity map all of the free regions in the UEFI memory map, it converts the virtual address into a pml4 index, then it checks whether all the necessary page tables for the mapping exist and if something is missing it uses "alloc_frame()" function to allocate more memory for the page tables if needed, it also uses "free_frame()" to free a page tables that isn't used/overwritten by huge pages, after the paging route used create mapping for all the free regions and all the MMIO regions it remaps the kernel and the framebuffer into virtual addresses 0xffff800000000000 and 0xffffa00000000000 respectively, then it loads cr3 with the pml4 base, sets up the paging flag to 1, and uses the "kernel_trampoline()" function to jump to the kernel's virtual entry.


## Post-Paging kernel
upon entering the kernel's virtual entry it checks the paging flag to see that it's 1, so it skips the paging_setup and moves on to intialize the hardware, it passes GOP info to it's own "vga subsystem" and also includes it's own 8x16 fonts to print characters on the screen, then it reloads the GDT, checks some CPU stuff and prints kernel logs to the screen.

## Verification
this operating system was tested on at least 6 different modern computers and is confirmed to work on real x86-64 UEFI compatible machines.

## Running in QEMU

Install QEMU and OVMF, then run:

```sh
./run-qemu.sh
```

The script builds the bootloader and kernel, then boots copies of them from a
temporary virtual FAT disk. Kernel text written through `draw_char` is mirrored
to QEMU's debug port (`0xE9`) and printed in the terminal while the graphical GOP
framebuffer is shown in a separate window. Pass `--headless` to disable the GOP
window, or `--no-build` to reuse existing binaries. The virtual VGA memory is
limited to 2 MB, making the bootloader select QEMU's small 832x624 GOP mode—the
closest mode QEMU exposes above 800x600. The launcher leaves the CPU model
unspecified so QEMU supplies its default CPUID configuration. Use `--cpu MODEL`
only when a particular virtual CPU configuration is needed.

### Live CPU visualizer

To stop at the UEFI bootloader's `efi_main` and watch the bootloader and kernel
execute instruction by instruction, run:

```sh
./visual-debugger.sh --hz 5 --run
```

The terminal interface displays changing CPU registers, upcoming assembly,
kernel debug output, and interrupt-controller state. Space pauses or resumes,
`s` advances one instruction, `+`/`-` changes the visible instruction rate,
and `q` exits. Add `--headless` to hide the separate GOP framebuffer or
`--no-build` to use the existing binaries. The selected rate is deliberately a
human-visible instruction rate; it is not a cycle-accurate model of a physical

The visualizer builds a temporary trap-enabled copy of `BOOTX64.efi`; the
ordinary bootloader image used by `run-qemu.sh` is not modified. When execution
reaches the kernel's fixed higher-half mapping, the display automatically
switches from bootloader symbols to kernel symbols.

For the legacy BIOS boot path, use the separate real-mode visualizer:

```sh
./bios-visual-debugger.sh --hz 5
```

It runs `bios_qemu.sh`, fast-forwards the PC through the BIOS firmware to the
first boot-sector instruction at `0000:7c00`, and initially pauses there. The
Space, `s`, `+`/`-`, `r`, and `q` run, single-step, change speed, refresh, and
quit. Press `b` to enter a breakpoint; QEMU then runs at full speed until it is
hit. A centered entry box accepts a physical/virtual address (`7c80`), `CS:IP`
(`1000:0040`), or a stage-qualified assembly label
(`stage3:end_of_chain`). The register and instruction views automatically
follow transitions between 16-bit real mode, 16/32-bit protected mode, and
64-bit long mode. The fetched machine-code view marks the live instruction with
`PC>`. Up/Down browse earlier or later instructions without executing them, and
fall back to byte-wise memory browsing outside known instruction ranges, including
below `0x7c00`; `r` returns the view to the live PC. The machine-code pane uses
all available terminal rows. Source code is not displayed; NASM metadata is
retained internally for named breakpoints and instruction boundaries. Pass
`--run` to begin stepping as soon as the boot sector is reached, or `--headless`
to suppress QEMU's display.

The stack pane displays raw stack slots and annotates values that match known
bootloader labels as possible return addresses. `SP>`, `ESP>`, or `RSP>` marks
the live pointer. `W`/`S` browse raw memory in both directions above and below
it without executing code; `->` marks the independent browsing cursor. Since
`S` is used for stack navigation, `N` performs a single-instruction step.

Addresses are rendered as `segment:offset` in real mode, padded 32-bit flat
addresses in protected mode without paging, and padded 64-bit addresses in long
mode. Protected- and long-mode addresses are identified as virtual whenever
paging is active.
