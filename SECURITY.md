# Security Policy

64-dem is a from-scratch x86-64 operating system (UEFI and legacy BIOS boot) written in C, C++ and assembly by a single maintainer. It is an early-stage, experimental project. This policy explains what is in scope, what to expect from the project, and how to report a problem privately.

## Project status and supported versions

64-dem has no tagged releases or version numbers. Development happens on the `master` branch.

| Version | Supported |
| ------- | --------- |
| Latest commit on `master` | Yes, best effort |
| Older commits | No |

Fixes are made on `master` only. Nothing is backported.

64-dem is a learning and research kernel. It is not hardened for production use and should not be used to protect sensitive data or to run untrusted workloads. See "Known limitations" below.

## Reporting a vulnerability

Please report suspected vulnerabilities privately. Do not open a public issue or pull request for a security problem.

**Preferred channel:** GitHub private vulnerability reporting. Go to the repository's **Security** tab and choose **Report a vulnerability**.

<!-- MAINTAINER: private vulnerability reporting must be switched on for this link to work.
     Settings > Code security > Private vulnerability reporting > Enable.
     If you prefer email, add it here as a second channel: "Email: <your address>". -->

If you cannot use that channel, open a public issue that says only that you have a security report and asks for a private way to send it. Do not include technical details in the issue.

### What to include

A useful report has:

- A short description of the problem and where it is (file, function, or boot stage; a commit hash helps).
- The impact you believe it has.
- Steps to reproduce. QEMU is enough (`./run-qemu.sh`, or `./bios_qemu.sh` for the BIOS path). If it needs real hardware, a specific device, or a crafted USB device or disk image, say which.
- A suggested fix, if you have one. This is optional.

Partial reports are welcome. You do not need a working exploit.

### What to expect

This is a one-person project, so these are goals, not guarantees.

- Acknowledgement of your report within 7 days.
- A first assessment (accepted, needs more information, or out of scope) within 14 days.
- Fixes on `master` as time allows, with severity and complexity deciding the order.
- Credit in the fix commit or release notes if you want it. Tell me if you prefer to stay anonymous.

Please allow up to 90 days for a fix before public disclosure. If the problem is actively harming people, or a fix is clearly impossible, we can agree an earlier date. I will not take action against anyone who reports in good faith and follows this policy.

## Scope

### In scope

The following components are in this repository and are in scope. Each is listed with the input it accepts from outside the code, because that is where security problems are most likely.

| Area | Location | Untrusted input |
| ---- | -------- | --------------- |
| UEFI bootloader (custom ELF loader, memory map and GOP setup, kernel handoff) | `EFI/boot/` | Contents of `/boot/kernel.elf` on the boot medium; firmware-provided memory map and device path |
| BIOS boot chain (stage 1 to stage 4, FAT32 parsing, real-mode wrapper, long-mode switch) | `boot/bios/`, `install-bios-usb.sh`, `bios_qemu.sh` | Boot medium contents (stage files, `kernel.elf`); BIOS-provided memory map |
| Kernel core: paging, bitmap frame allocator, virtual address allocator, `kmalloc`, DMA allocator, GDT/TSS/IDT, interrupt handlers, APIC/IOAPIC | `boot/kernel/x86-64/` | Firmware-provided ACPI tables and memory map; device interrupts |
| PCI enumeration and IRQ routing | `boot/kernel/drivers/pci/` | Device-reported configuration space |
| USB stack: xHCI, EHCI, OHCI, UHCI controllers, device enumeration, descriptor parsing, periodic and async transfers | `boot/kernel/drivers/usb/` | Descriptors and data from attached USB devices |
| Input drivers: USB HID keyboard, PS/2 keyboard | `boot/kernel/drivers/keyboard/` | HID reports and scancodes from attached devices |
| Storage drivers: ATA/IDE, SATA, USB mass storage | `boot/kernel/drivers/storage/` | Data and identification responses from drives |
| Display, timers (HPET, PM timer, RTC), PC speaker, ACPI power management | `boot/kernel/drivers/` | Firmware tables and hardware registers |
| Build and install scripts | `make.sh`, `boot/build.sh`, `EFI/boot/make.sh`, `install-bios-usb.sh`, `run-qemu.sh` | Command-line arguments and environment variables |

Examples of reports that are in scope:

- Memory corruption (out-of-bounds read or write, use-after-free, double free, integer overflow) reachable from a malformed ELF file, USB descriptor, ACPI table, memory map, PCI configuration space, or disk response.
- A crafted USB device or boot medium that causes the kernel to hang, crash, or run attacker-chosen code.
- DMA buffers or MMIO mappings that are mapped, sized, or freed incorrectly.
- The install script (`install-bios-usb.sh`) writing to a device the user did not select, or its safety checks being bypassable.
- A defect that makes a build script run attacker-influenced input as code.

### Out of scope

- **Physical access and hardware attacks.** An attacker who can attach arbitrary hardware, flash firmware, or modify the boot medium already controls the machine. Reports that depend on this alone are out of scope, unless they show a flaw in how 64-dem handles that input (for example, a malformed device that corrupts kernel memory).
- **Missing features.** 64-dem currently has no user mode, no system calls, no networking, no filesystem driver in the kernel, no authentication, and no persistent user data. Absence of these is a design stage, not a vulnerability. Reports about them will be closed as out of scope. If any of these features are added later, this policy will be updated.
- **Hardening that is not yet implemented.** See "Known limitations". A report that only says a hardening measure is missing is welcome as a normal issue or pull request, not as a private report.
- **Firmware, hypervisor, or toolchain bugs** in UEFI/BIOS implementations, OVMF, QEMU, GCC, or NASM. Please report those to their own projects. If 64-dem triggers a bug in one of them, a heads-up is still appreciated.
- **Developer tooling run locally:** `visual-debugger.py`, `bios-visual-debugger.py` and the helper scripts in `python/` are debugging aids for the maintainer's own machine. The visualizer opens a QEMU monitor socket and a GDB port on `127.0.0.1` for the duration of a debug session. Behaviour that requires an attacker who already has local access to the developer's machine is out of scope.
- Third-party code and data not written for this project, such as the generated PCI vendor and device name table, beyond how 64-dem uses them.
- Social engineering, spam, and denial of service against GitHub or the maintainer personally.

## Known limitations

To save everyone's time, these are the current, known gaps. They are documented as design limits of an early kernel, not as secret flaws.

- **Single privilege level.** All code runs in ring 0. There is no user mode and no isolation between kernel components or drivers.
- **No memory protection hardening.** Kernel page mappings are created with basic present/writable flags. The mapping interface takes a 16-bit attribute value, so it cannot set the no-execute bit, and the code does not enable NX, SMEP, SMAP, stack canaries or address-space randomization. Memory is writable and executable wherever it is mapped.
- **No IOMMU.** DMA-capable devices (xHCI, EHCI, OHCI, UHCI, SATA/ATA) are not restricted by an IOMMU. A malicious or compromised device can reach any physical memory.
- **No boot integrity checks.** The boot chain does not verify signatures or hashes. UEFI Secure Boot is not supported, and the bootloader and BIOS stages trust the contents of the boot medium. The UEFI loader checks the ELF magic bytes before loading `/boot/kernel.elf`; it does not otherwise authenticate it. Anyone who can write to the boot medium can replace the kernel.
- **Trust in firmware data.** ACPI tables, the memory map and PCI configuration space are treated as coming from trusted firmware and hardware. The USB stack does validate configuration descriptor sizes and bounds before parsing them, but this is not a substitute for a full audit, and other parsers may be less strict.
- **Early-stage drivers.** The USB mass storage and SATA drivers are under active development (recent commits are titled "worked on SATA" and "worked on USB mass storage"). Write paths exist in the ATA and USB mass storage drivers (`ata_write`, `ums_write`); data written through them should not be considered safe from corruption.
- **Test coverage.** Host-side unit tests live in `tests/` (run with `tests/run_tests.sh`) and cover parts of the USB, HID, DMA, virtual allocator, PCI IRQ and ATA identify code. There is no automated fuzzing or continuous integration at this time.

## Safe use of the install script

`install-bios-usb.sh` repartitions and erases a whole disk using `sudo`. It refuses partitions, mounted devices and the disk holding the running root filesystem, refuses devices not marked removable unless you pass `--allow-non-removable`, and asks you to type the device path to confirm unless you pass `--yes`. Read the output of `lsblk` carefully before you confirm. Do not use `--yes` or `--allow-non-removable` unless you are certain of the device. Data loss from running the script on the wrong device is a user error, not a vulnerability, but a bypass of the script's own safety checks is in scope.

## Binaries in the repository

The repository includes prebuilt files such as `EFI/boot/BOOTX64.efi`, `boot/kernel.elf`, `boot/stage2.bin`, `boot/stage3.bin` and `boot/stage4.elf`. These are convenience builds. For anything that matters, build from source with `./make.sh` and compare, and do not run binaries from an unofficial copy of the repository. The project does not currently publish checksums or signatures for them.

## Safe harbor

If you act in good faith to find and report a vulnerability, test only on hardware, virtual machines and storage media you own or are authorized to use, avoid harming other people's data or systems, and give reasonable time to fix before disclosing, then I will treat your work as authorized and will not pursue action against you.

## Licensing and disclaimer

There is no currently assigned license for this project, it should be assigned one ;)

_Last reviewed against commit `120960a` on `master`._
