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
