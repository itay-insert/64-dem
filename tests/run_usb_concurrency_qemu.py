#!/usr/bin/env python3
"""Build an isolated test kernel and check keyboard/storage concurrency in QEMU.

Requires the regular kernel/UEFI build dependencies, QEMU, OVMF, dosfstools,
util-linux (sfdisk), and mtools. No production sources or disks are modified.
"""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parent.parent
CONTROLLERS = {
    "xhci": "qemu-xhci",
    "ehci": "usb-ehci",
    "ohci": "pci-ohci",
    "uhci": "piix3-usb-uhci",
}


def prepare(root):
    for name in ("boot", "EFI"):
        shutil.copytree(REPO / name, root / name, ignore=shutil.ignore_patterns("*.o"))
    for name in ("make.sh", "bios_qemu.sh"):
        shutil.copy2(REPO / name, root / name)
    shutil.copy2(REPO / "tests/usb_concurrency_probe.hpp", root / "boot/kernel/usb_concurrency_probe.hpp")
    kernel = root / "boot/kernel/kernel.cpp"
    source = kernel.read_text()
    marker = "    u64 old_keyboard_irq = keyboard_irq_count;"
    if source.count(marker) != 1:
        raise RuntimeError("Cannot locate foreground loop for test-kernel injection")
    # Replace only the temporary kernel's tail. The integration probe controls
    # foreground service, independent of whatever boot-device work is in progress.
    source = source[:source.index(marker)] + """
    usb_concurrency_probe(USB, *timer.Src);
    for (;;) { USB.service_interrupts(*timer.Src); wait_ms(*timer.Src, 1); }
}
"""
    kernel.write_text('#include "usb_concurrency_probe.hpp"\n' + source)
    make = root / "make.sh"
    make.write_text("set -e\n" + make.read_text())
    env = os.environ.copy()
    env["PATH"] += ":/usr/sbin:/sbin"
    with (root / "build.log").open("w") as log:
        subprocess.run(["bash", "./bios_qemu.sh", "--no-run", "--image", str(root / "boot.img")],
                       cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    if not (root / "boot/kernel.elf").is_file():
        raise RuntimeError("Build produced no kernel ELF; see build.log")
    with (root / "pattern.img").open("wb") as disk:
        for lba in range(4096):
            disk.write(bytes((lba * 17 + offset * 13) & 255 for offset in range(512)))


def run_case(root, firmware, controller, mode, timeout):
    name = f"{controller}-{mode}"
    log = root / f"{name}.log"
    monitor = root / f"{name}.sock"
    stderr_path = root / f"{name}.stderr"
    device = CONTROLLERS[controller] + ",id=usbtest"
    if controller == "xhci":
        device += f",msi={'on' if mode == 'msi' else 'off'},msix={'on' if mode == 'msix' else 'off'}"
    command = [
        "qemu-system-x86_64", "-machine", "pc", "-m", "512M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={firmware}",
        "-display", "none", "-serial", "none", "-no-reboot",
        "-d", "int", "-D", str(root / f"{name}.trace"),
        "-drive", f"format=raw,file={root}/boot.img,snapshot=on",
        "-vga", "std", "-global", "VGA.vgamem_mb=2", "-debugcon", f"file:{log}",
        "-qmp", f"unix:{monitor},server=on,wait=off",
        "-device", device, "-device", "usb-kbd,bus=usbtest.0,port=1",
        "-drive", f"if=none,id=stick,format=raw,file={root}/pattern.img,readonly=on",
        "-device", "usb-storage,bus=usbtest.0,port=2,drive=stick",
    ]
    content = ""
    with stderr_path.open("w") as stderr:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=stderr)
        try:
            deadline = time.monotonic() + timeout
            while not monitor.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.05)
            if not monitor.exists():
                raise RuntimeError(f"QEMU failed to open monitor: {stderr_path.read_text()}")
            with socket.socket(socket.AF_UNIX) as connection:
                connection.connect(str(monitor))
                connection.settimeout(5)
                with connection.makefile("rb") as stream:
                    stream.readline()  # QMP greeting

                    def qmp(execute, arguments=None):
                        message = {"execute": execute}
                        if arguments is not None:
                            message["arguments"] = arguments
                        connection.sendall((json.dumps(message) + "\n").encode())
                        while True:
                            line = stream.readline()
                            if not line:
                                raise RuntimeError("QEMU monitor closed")
                            response = json.loads(line)
                            if "error" in response:
                                raise RuntimeError(f"QMP failed: {response['error']}")
                            if "return" in response:
                                return

                    qmp("qmp_capabilities")
                    next_key = 0
                    while time.monotonic() < deadline and process.poll() is None:
                        content = log.read_text(errors="replace") if log.exists() else ""
                        if "CONCURRENCY TYPING" in content and time.monotonic() >= next_key:
                            qmp("human-monitor-command", {"command-line": "sendkey a"})
                            next_key = time.monotonic() + 0.12
                        if "CONCURRENCY FAIL" in content or "CONCURRENCY PASS" in content:
                            break
                        time.sleep(0.03)
            lines = [line for line in content.splitlines() if "CONCURRENCY" in line]
            print(name + ":\n" + "\n".join(lines), flush=True)
            if "CONCURRENCY PASS" not in content:
                raise RuntimeError(f"{name} failed/timed out; see {log} and {stderr_path}")
            if "idle_reads=16 keys=0" not in content:
                raise RuntimeError(f"{name} did not exercise the idle keyboard case")
            if controller == "xhci":
                routed = "MSI IRQ vector" if mode == "msi" else "MSI-X IRQ vector"
                if mode != "polling" and routed not in content:
                    raise RuntimeError(f"{name} did not install its requested IRQ route")
                trace = (root / f"{name}.trace").read_text(errors="replace")
                deliveries = len(re.findall(r"v=50[^\n]*pc=ffff8", trace))
                if (mode != "polling" and deliveries < 2) or (mode == "polling" and deliveries):
                    raise RuntimeError(f"{name}: unexpected hardware IRQ deliveries: {deliveries}")
                print(f"{name}: kernel vector 0x50 deliveries={deliveries}", flush=True)
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controller", choices=["all", *CONTROLLERS], default="all")
    parser.add_argument("--irq-mode", choices=["all", "msi", "msix", "polling"], default="all")
    parser.add_argument("--firmware", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--timeout", type=int, default=180, help="seconds per QEMU case")
    parser.add_argument("--work-dir", type=Path, help="new or empty directory to retain build/log artifacts")
    args = parser.parse_args()
    if not args.firmware.is_file():
        parser.error("OVMF firmware missing; supply --firmware")
    temporary = None
    if args.work_dir:
        root = args.work_dir.resolve()
        root.mkdir(parents=True, exist_ok=True)
        if any(root.iterdir()):
            parser.error("--work-dir must be empty")
    else:
        temporary = tempfile.TemporaryDirectory(prefix="64-dem-usb-concurrency-")
        root = Path(temporary.name)
    print(f"Building test kernel in {root}", flush=True)
    try:
        prepare(root)
        controllers = CONTROLLERS if args.controller == "all" else [args.controller]
        for controller in controllers:
            modes = (["msi", "msix", "polling"] if args.irq_mode == "all" else [args.irq_mode]) \
                if controller == "xhci" else ["polling"]
            for mode in modes:
                run_case(root, args.firmware.resolve(), controller, mode, args.timeout)
    except Exception:
        print(f"Artifacts: {root}; use --work-dir to retain them after exit", flush=True)
        raise
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    main()
