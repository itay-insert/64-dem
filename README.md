# 64-dem - A From-Scratch x86-64 Operating System 
64-dem is a 64-bit UEFI operating system booting from a USB flash drive, made for the x86-64/amd64 architecture.

## License
64-dem is licensed under the GNU General Public License version 2 only (GPL-2.0-only). 

## The bootloader 
At boot the UEFI bootloader gets the memory map from UEFI using a UEFI call, it also sets up the GOP picking the highest resolution it can find, after the GOP is setup the bootloader uses UEFI calls to read the file stored in path "/boot/kernel.elf", it uses a custom elf parser to parse the elf file, at first it calculates the size of all the segments in pages then allocates memory using it's own custom memory allocator, after allocating enough memory it parses the elf file, copies the segments to the right memory regions, exits UEFI services, disables interrupts and jumps to the kernel entry passing some parameters along the way.


## bios boot path
the os also supports a separate bios boot path.

### stage1
with the 16-bit stage1 bios bootloader being 440 bytes in the Boot sector, upon being loaded to 0x7c00, the bootloader uses a tiny optimized fat32 parser to locate and load stage2.bin from the boot directory of the boot drive to memory at address 0x8000, where the bootloader transfers control to stage2.

### stage2
stage2.bin is 512 bytes long, where it includes an even smarter parser, uses stage1 services through function pointers, locates and loads stage3.bin from the boot directory to address 0x10000 where control is transfered to stage3.

### stage3
stage3 is also 16-bit however this time it is 32kb maximum, allowing an even smarter parser and an elf header, stage3 establishes the bios services table which would be passed to stage4, stage3 uses the fat32 parser to locate and load the first few clusters of stage4.elf then it parses the elf header using an elf parser in assembly, after loading all the segments to their addresses the stage3 loader switches to 32-bit protected mode by loading the gdt with a 32-bit code segment, enables the a20 line and jumps to the stage4 entry at address 0x18000.

### stage4
stage4 is already in 32-bit protected mode, written in c instead of assembly,and uses the bios services table passed from stage3.

#### emergency IDT setup to handle NMIs
one of the big traps when entering protected mode are NMIs, unlike regular interrupts which are usually off when cli is set, Unmaskable interrupts can occur either way, usually the IVT in real mode contains handlers for that but on 32-bit protected mode we use the idt instead, without a proper handler to catch the unmaskable interrupts a triple fault can occur soon on real hardware even though it seems as if we got everything right, this behavior might not happen on VMs like QEMU which are nicer on NMIs but on real hardware an NMI is much more likely to occur which is why a proper NMI handler is needed upon entering protected mode, and stage4 sets up such handler upon entry, likewise it did fix the fault errors when testing the bios boot path on real modern hardware.

#### RealModeWrapper 
using a special RealModeWrapper, the stage4 is able to use stage3 bios services functions to load sectors from the disk or set a framebuffer even when in 32-bit protected mode, the way this works is the real mode wrapper loads the address of the real mode function as well as the arguments, reloads the gdt with a 16-bit segment to switch back to real mode and when the function is done it reloads the gdt back with a 32-bit segment to return to protected mode, it may or may not return a value to the caller depending on the selected service.

#### kernel environment setup
using the RealModeWrapper the 32-bit stage4 sets up the framebuffer using bios Vbe services, a legacy equivalent to the modern GOP, stage4 picks the highest resolution and saves the important framebuffer info, afterwards it uses the RealModeWrapper to obtain the bios memory map which it converts to a UEFI compatible memory map, it is done as a clever workaround since my kernel wasn't originally coded to support bios boot. anyways after setting the memory map and screen, stage4 uses it's own graphics subsystem to temporarily print messages, it organizes a data table matching the kernel ABI that would be passed to it, uses bios_disk_services to locate kernel.elf inside the boot directory and after it finds it, it loads the header, and reads the kernel file segments, since the kernel is meant to be loaded in the higher addresses (0x100000+) which aren't supported in real-mode, the kernel loader uses a back and forth mechanism to load 4 kilobytes at a time to a special low region in ram (the 0x95000 region) and later copies it to the higher address, the kernel base is picked by a tiny algorithm that scans the UEFI converted memory map, the algorithm also allocates a stack and a bitmap region depending on the memory size needed.

#### long mode transition and paging setup
to transition to long mode, stage4 must setup basic 4-level paging and a few bits in the control registers as well as gdt reload with a 64-bit segment, to start with the most basic 4-level paging i created the PML4 table at 0x90000, the first PDPT table at 0x91000 and the first PD table at 0x92000, rhe hierarchy is essentially PML4->PDPT->PD, since there is only one PDPT and one PD it's only enough to map 1 Gigabyte with each PD entry corresponding to a 2 MiB page, this is essentially enough for all we need since the kernel is expected to be far less than 1 Gigabyte, after setting up the tables we pass the address of PML4 (0x90000) to cr3, set some bits in cr0, reload the gdt with a 64-bit code segment and make a far jump to the long mode entry, where stage4 immediately sets up a 64-bit NMI handler to avoid the same NMI triple fault the stage4 protected mode entry avoided earlier (32-bit protected mode and 64-bit long mode both use different IDT layouts) then it sets up the data table pointers and kernel arguments in appropriate registers matching the ABI System V calling convention and finally jumps to the kernel entry and transfers control to my kernel which would eventually re-arrange paging by itself as well as reset the idt with execptions and proper interrupt handlers.

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
