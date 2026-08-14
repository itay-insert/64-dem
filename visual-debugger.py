#!/usr/bin/env python3
"""Small, dependency-free live CPU view for the 64-dem QEMU guest."""

from __future__ import annotations

import argparse
import ast
import curses
import os
import queue
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parent
KERNEL_VIRTUAL_BASE = 0xFFFF800000000000
IMPORTANT_REGS = (
    "rip", "cr0", "cr2", "cr3", "cr4", "eflags", "rsp",
    "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "cs", "ss",
)


def elf_load_layout(path: Path) -> tuple[int, int]:
    """Return (entry, lowest PT_LOAD virtual address) for a little-endian ELF64."""
    with path.open("rb") as elf:
        header = elf.read(64)
        if header[:5] != b"\x7fELF\x02" or header[5] != 1:
            raise RuntimeError(f"unsupported kernel ELF format: {path}")
        entry = struct.unpack_from("<Q", header, 24)[0]
        phoff = struct.unpack_from("<Q", header, 32)[0]
        phentsize = struct.unpack_from("<H", header, 54)[0]
        phnum = struct.unpack_from("<H", header, 56)[0]
        load_addresses = []
        for index in range(phnum):
            elf.seek(phoff + index * phentsize)
            program_header = elf.read(phentsize)
            if struct.unpack_from("<I", program_header, 0)[0] == 1:
                load_addresses.append(struct.unpack_from("<Q", program_header, 16)[0])
    if not load_addresses:
        raise RuntimeError("kernel ELF has no loadable segments")
    return entry, min(load_addresses)


def symbol_address(path: Path, symbol: str) -> int:
    output = subprocess.check_output(["nm", "-n", str(path)], text=True)
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[2] == symbol:
            return int(fields[0], 16)
    raise RuntimeError(f"symbol {symbol!r} was not found in {path}")


def mi_unquote(value: str) -> str:
    try:
        return ast.literal_eval(value)
    except (SyntaxError, ValueError):
        return value.strip('"')


class GdbMI:
    def __init__(self, executable: str):
        self.proc = subprocess.Popen(
            [executable, "--quiet", "--nx", "--interpreter=mi2"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        self.lines: queue.Queue[str] = queue.Queue()
        self.token = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        assert self.proc.stdout
        for line in self.proc.stdout:
            self.lines.put(line.rstrip("\n"))

    def command(self, command: str, timeout: float = 10.0) -> list[str]:
        self.token += 1
        token = str(self.token)
        assert self.proc.stdin
        self.proc.stdin.write(token + command + "\n")
        self.proc.stdin.flush()
        result: list[str] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty:
                break
            result.append(line)
            if re.match(rf"^{re.escape(token)}\^(done|running|connected|exit)(?:,|$)", line):
                return result
            if line.startswith(token + "^error"):
                message = re.search(r'msg=("(?:[^"\\]|\\.)*")', line)
                raise RuntimeError(mi_unquote(message.group(1)) if message else line)
        raise TimeoutError(f"GDB timed out while running {command}")

    def wait_stopped(self, timeout: float = 30.0) -> list[str]:
        result: list[str] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty:
                break
            result.append(line)
            if line.startswith("*stopped"):
                return result
        raise TimeoutError("guest did not stop")

    def console(self, command: str) -> str:
        escaped = command.replace("\\", "\\\\").replace('"', '\\"')
        lines = self.command(f'-interpreter-exec console "{escaped}"')
        output = []
        for line in lines:
            if line.startswith("~"):
                output.append(mi_unquote(line[1:]))
        return "".join(output)

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.command("-gdb-exit", timeout=1)
            except (RuntimeError, TimeoutError, BrokenPipeError):
                self.proc.terminate()


class HMP:
    def __init__(self, path: Path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(2)
        self.sock.connect(str(path))
        self._receive_prompt()

    def _receive_prompt(self) -> str:
        data = bytearray()
        while not data.endswith(b"(qemu) "):
            chunk = self.sock.recv(65536)
            if not chunk:
                break
            data.extend(chunk)
        text = data.decode(errors="replace")
        text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
        return "".join(character for character in text
                       if character in "\n\r\t" or ord(character) >= 32)

    def command(self, command: str) -> str:
        self.sock.sendall(command.encode() + b"\n")
        text = self._receive_prompt().replace("\r", "")
        lines = text.splitlines()
        # HMP's readline frontend echoes the command using cursor-control
        # sequences; after sanitizing those sequences the echo is garbled.
        if lines:
            lines.pop(0)
        return "\n".join(line for line in lines if line.strip() != "(qemu)").strip()

    def close(self) -> None:
        self.sock.close()


class Visualizer:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.rate = args.hz
        self.running = args.run
        self.quit = False
        self.step_requested = False
        self.registers: dict[str, str] = {}
        self.old_registers: dict[str, str] = {}
        self.disassembly: list[str] = []
        self.hardware: list[str] = []
        self.log: deque[str] = deque(maxlen=200)
        self.status = "starting"
        self.last_hardware = 0.0
        self.bootloader_ready = threading.Event()
        self.qemu: subprocess.Popen[str] | None = None
        self.gdb: GdbMI | None = None
        self.hmp: HMP | None = None
        self.tempdir: tempfile.TemporaryDirectory[str] | None = None
        self.kernel_symbols = False

    def start(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory(prefix="64-dem-visual-")
        monitor = Path(self.tempdir.name) / "monitor.sock"
        debug_efi = Path(self.tempdir.name) / "BOOTX64-debug.efi"
        debug_elf = Path(self.tempdir.name) / "bootloader-debug.so"
        if not self.args.no_build:
            subprocess.run(["bash", str(ROOT / "make.sh")], cwd=ROOT, check=True)
        build_environment = os.environ.copy()
        build_environment.update({
            "VISUAL_DEBUGGER": "1",
            "BOOTLOADER_OUTPUT": str(debug_efi),
            "BOOTLOADER_DEBUG_ELF": str(debug_elf),
        })
        subprocess.run(["bash", "./make.sh"], cwd=ROOT / "EFI/boot",
                       env=build_environment, check=True)
        run_args = [str(ROOT / "run-qemu.sh"), "--no-build", "--bootloader",
                    str(debug_efi), "--accel", "tcg,thread=single"]
        if self.args.headless:
            run_args.append("--headless")
        run_args += ["--", "-S", "-gdb", f"tcp:127.0.0.1:{self.args.port}",
                     "-monitor", f"unix:{monitor},server=on,wait=off",
                     "-icount", "shift=0,align=off,sleep=off"]
        self.qemu = subprocess.Popen(
            run_args, cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        threading.Thread(target=self._read_qemu, daemon=True).start()

        self.gdb = GdbMI(self.args.gdb)
        self.gdb.command("-gdb-set mi-async on")
        deadline = time.monotonic() + 45
        while True:
            self._check_qemu()
            try:
                self.gdb.command(f"-target-select remote 127.0.0.1:{self.args.port}", timeout=1)
                break
            except (RuntimeError, TimeoutError):
                if time.monotonic() >= deadline:
                    raise RuntimeError("timed out waiting for QEMU's GDB server")
                time.sleep(0.1)
        self.gdb.console("set disassembly-flavor intel")
        self.status = "fast-forwarding through UEFI firmware to BOOTX64.efi"
        self.gdb.command("-exec-continue")
        deadline = time.monotonic() + 60
        while not self.bootloader_ready.wait(0.05):
            self._check_qemu()
            if time.monotonic() >= deadline:
                raise TimeoutError("bootloader debug handshake did not arrive")
        self.gdb.command("-exec-interrupt")
        self.gdb.wait_stopped(timeout=5)
        pc_text = self.gdb.console("print/x $pc")
        pc_match = re.search(r"0x[0-9a-fA-F]+", pc_text)
        if not pc_match:
            raise RuntimeError("could not read the bootloader program counter")
        runtime_trap = int(pc_match.group(0), 16)
        linked_trap = symbol_address(debug_elf, "visual_debugger_trap")
        self.gdb.console(
            f"symbol-file -o 0x{runtime_trap - linked_trap:x} {debug_elf}")
        self.gdb.console("set $pc = visual_debugger_resume")

        deadline = time.monotonic() + 5
        while not monitor.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        if monitor.exists():
            self.hmp = HMP(monitor)
        self.status = "paused at bootloader efi_main"
        self.refresh(force_hardware=True)

    def _read_qemu(self) -> None:
        assert self.qemu and self.qemu.stdout
        for line in self.qemu.stdout:
            clean_line = line.rstrip()
            self.log.append(clean_line)
            if clean_line == "VDBG":
                self.bootloader_ready.set()

    def _check_qemu(self) -> None:
        if self.qemu and self.qemu.poll() is not None:
            detail = "\n".join(self.log)
            raise RuntimeError(f"QEMU exited with status {self.qemu.returncode}\n{detail}")

    def step(self) -> None:
        assert self.gdb
        self.gdb.command("-exec-step-instruction")
        self.gdb.wait_stopped(timeout=5)
        self.refresh()

    def refresh(self, force_hardware: bool = False) -> None:
        assert self.gdb
        self.old_registers = self.registers
        raw = self.gdb.console("info registers")
        registers = {}
        for line in raw.splitlines():
            match = re.match(r"(\S+)\s+(0x[0-9a-fA-F]+|[-]?[0-9]+)", line)
            if match:
                registers[match.group(1)] = match.group(2)
        self.registers = registers
        rip = int(registers.get("rip", "0"), 0)
        if rip >= KERNEL_VIRTUAL_BASE and not self.kernel_symbols:
            kernel = ROOT / "boot/kernel.elf"
            _, linked_base = elf_load_layout(kernel)
            relocation = KERNEL_VIRTUAL_BASE - linked_base
            self.gdb.console(f"symbol-file -o 0x{relocation:x} {kernel}")
            self.kernel_symbols = True
            self.status = "executing kernel with virtual symbols loaded"
        self.disassembly = self.gdb.console("x/14i $pc").splitlines()
        now = time.monotonic()
        if self.hmp and (force_hardware or now - self.last_hardware >= 1):
            sections = []
            for title, command in (("IRQ", "info irq"), ("PIC", "info pic"),
                                   ("LAPIC", "info lapic")):
                response = self.hmp.command(command)
                if "unknown command" not in response.lower():
                    sections.extend([f"[{title}]", *response.splitlines()])
            self.hardware = sections
            self.last_hardware = now

    def cleanup(self) -> None:
        if self.hmp:
            self.hmp.close()
        if self.gdb:
            self.gdb.close()
        if self.qemu and self.qemu.poll() is None:
            self.qemu.terminate()
            try:
                self.qemu.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.qemu.kill()
        if self.tempdir:
            self.tempdir.cleanup()

    @staticmethod
    def _box(win: curses.window, title: str) -> None:
        win.erase()
        win.box()
        try:
            win.addstr(0, 2, f" {title} ", curses.A_BOLD)
        except curses.error:
            pass

    @staticmethod
    def _lines(win: curses.window, lines: list[str], changed: set[str] | None = None) -> None:
        height, width = win.getmaxyx()
        for row, line in enumerate(lines[:height - 2], start=1):
            attr = curses.A_NORMAL
            if changed and line.split(maxsplit=1)[0].lower() in changed:
                attr = curses.A_BOLD | curses.A_REVERSE
            try:
                win.addnstr(row, 1, line, width - 2, attr)
            except curses.error:
                pass
        win.noutrefresh()

    def draw(self, screen: curses.window) -> None:
        height, width = screen.getmaxyx()
        screen.erase()
        if height < 18 or width < 80:
            screen.addstr(0, 0, "Terminal must be at least 80x18. Resize or press q.")
            screen.refresh()
            return
        top = height - 5
        left_width = max(29, width // 3)
        right_width = width - left_width
        reg_win = screen.derwin(top // 2, left_width, 0, 0)
        hw_win = screen.derwin(top - top // 2, left_width, top // 2, 0)
        asm_win = screen.derwin(top * 2 // 3, right_width, 0, left_width)
        log_win = screen.derwin(top - top * 2 // 3, right_width, top * 2 // 3, left_width)
        status_win = screen.derwin(5, width, top, 0)

        changed = {name for name, value in self.registers.items()
                   if self.old_registers.get(name) != value}
        reg_lines = [f"{name.upper():7} {self.registers.get(name, '—')}"
                     for name in IMPORTANT_REGS if name in self.registers]
        self._box(reg_win, "CPU REGISTERS")
        self._lines(reg_win, reg_lines, changed)
        self._box(hw_win, "EMULATED HARDWARE")
        self._lines(hw_win, self.hardware or ["Hardware monitor unavailable"])
        self._box(asm_win, "NEXT INSTRUCTIONS")
        self._lines(asm_win, self.disassembly)
        self._box(log_win, "KERNEL / QEMU OUTPUT")
        self._lines(log_win, list(self.log)[-(log_win.getmaxyx()[0] - 2):])
        self._box(status_win, "CONTROL")
        mode = f"RUNNING @ {self.rate:g} instr/s" if self.running else "PAUSED"
        controls = "SPACE run/pause   S step   +/- rate   R refresh   Q quit"
        self._lines(status_win, [f"{mode}  |  {self.status}", controls])
        curses.doupdate()

    def ui(self, screen: curses.window) -> None:
        curses.curs_set(0)
        screen.nodelay(True)
        next_step = time.monotonic()
        while not self.quit:
            self.draw(screen)
            key = screen.getch()
            if key in (ord("q"), ord("Q")):
                self.quit = True
            elif key == ord(" "):
                self.running = not self.running
                self.status = "running" if self.running else "paused"
                next_step = time.monotonic()
            elif key in (ord("s"), ord("S")):
                self.running = False
                self.step_requested = True
            elif key in (ord("+"), ord("=")):
                self.rate = min(1000.0, self.rate * 2)
            elif key in (ord("-"), ord("_")):
                self.rate = max(0.25, self.rate / 2)
            elif key in (ord("r"), ord("R")):
                self.refresh(force_hardware=True)

            now = time.monotonic()
            if self.step_requested or (self.running and now >= next_step):
                self.step_requested = False
                self.step()
                next_step = time.monotonic() + 1 / self.rate
            time.sleep(0.01)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Watch the 64-dem bootloader and kernel execute in QEMU.")
    parser.add_argument("--hz", type=float, default=5.0,
                        help="visible instruction rate (default: 5)")
    parser.add_argument("--run", action="store_true",
                        help="start ticking immediately after reaching efi_main")
    parser.add_argument("--no-build", action="store_true",
                        help="reuse existing bootloader and kernel binaries")
    parser.add_argument("--headless", action="store_true",
                        help="do not open the guest framebuffer window")
    parser.add_argument("--port", type=int, default=1234,
                        help="localhost GDB port (default: 1234)")
    parser.add_argument("--gdb", default=os.environ.get("GDB", "gdb"),
                        help="GDB executable (default: gdb or $GDB)")
    args = parser.parse_args()
    if not 0.25 <= args.hz <= 1000:
        parser.error("--hz must be between 0.25 and 1000")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    return args


def main() -> int:
    args = parse_args()
    for program in (args.gdb, "qemu-system-x86_64", "nm", "gcc", "ld", "objcopy"):
        if not shutil.which(program):
            print(f"error: required program not found: {program}", file=sys.stderr)
            return 1
    visualizer = Visualizer(args)
    try:
        visualizer.start()
        curses.wrapper(visualizer.ui)
    except KeyboardInterrupt:
        pass
    except Exception as error:
        print(f"visual debugger error: {error}", file=sys.stderr)
        return 1
    finally:
        visualizer.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
