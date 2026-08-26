#!/usr/bin/env python3
"""Interactive real-mode CPU view for the 64-dem BIOS boot path."""

from __future__ import annotations

import argparse
import ast
import bisect
import curses
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parent
REGISTERS = (
    "cs", "rip", "ss", "rsp", "ds", "es", "fs", "gs", "eflags",
    "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp",
    "cr0", "cr2", "cr3", "cr4", "efer",
)
STAGES = (
    ("stage 1", ROOT / "boot/bios/boot.asm", 0x7C00, 0x8000),
    ("stage 2", ROOT / "boot/bios/stage2.asm", 0x8000, 0x8200),
    ("stage 3", ROOT / "boot/bios/stage3.asm", 0x10000, 0x18000),
)


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
            if re.match(rf"^{token}\^(done|running|connected|exit)(?:,|$)", line):
                return result
            if line.startswith(token + "^error"):
                match = re.search(r'msg=("(?:[^"\\]|\\.)*")', line)
                raise RuntimeError(mi_unquote(match.group(1)) if match else line)
        raise TimeoutError(f"GDB timed out while running {command}")

    def wait_stopped(self, timeout: float = 30.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty:
                break
            if line.startswith("*stopped"):
                return
        raise TimeoutError("guest did not stop")

    def console(self, command: str) -> str:
        escaped = command.replace("\\", "\\\\").replace('"', '\\"')
        lines = self.command(f'-interpreter-exec console "{escaped}"')
        return "".join(mi_unquote(line[1:]) for line in lines if line.startswith("~"))

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.command("-gdb-exit", timeout=1)
            except (RuntimeError, TimeoutError, BrokenPipeError):
                self.proc.terminate()


class BiosVisualizer:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.rate = args.hz
        self.running = args.run
        self.done = False
        self.registers: dict[str, str] = {}
        self.previous: dict[str, str] = {}
        self.disassembly: list[str] = []
        self.instruction_addresses: list[int] = []
        self.view_address: int | None = None
        self.pc_address = 0
        self.log: deque[str] = deque(maxlen=300)
        self.status = "starting QEMU"
        self.location = "firmware"
        self.cpu_mode = "real mode"
        self.code_bits = 16
        self.paging_enabled = False
        self.image_ready = threading.Event()
        self.qemu: subprocess.Popen[str] | None = None
        self.gdb: GdbMI | None = None
        self.tempdir: tempfile.TemporaryDirectory[str] | None = None
        self.source_maps: dict[Path, dict[int, int]] = {}
        self.labels: dict[str, int] = {}
        self.labels_by_address: dict[int, str] = {}
        self.memory_cache: dict[int, bytes] = {}
        self.stack_offset = 0
        self.stack_lines: list[str] = []
        self.stack_pointer_line = ""
        self.stack_register = "SP"

    def _make_source_maps(self) -> None:
        assert self.tempdir
        for name, source, base, _ in STAGES:
            listing = Path(self.tempdir.name) / f"{name.replace(' ', '-')}.lst"
            binary = Path(self.tempdir.name) / f"{name.replace(' ', '-')}.bin"
            subprocess.run(
                ["nasm", "-f", "bin", "-l", str(listing), str(source),
                 "-o", str(binary)], cwd=ROOT, check=True,
                stdout=subprocess.DEVNULL,
            )
            mapping: dict[int, int] = {}
            instructions: list[int] = []
            source_lines = source.read_text(errors="replace").splitlines()
            for line in listing.read_text(errors="replace").splitlines():
                match = re.match(r"\s*(\d+)\s+([0-9A-Fa-f]{8})\s+", line)
                if match:
                    address = base + int(match.group(2), 16)
                    line_number = int(match.group(1))
                    mapping[address] = line_number
                    if self._is_instruction(source_lines[line_number - 1]):
                        instructions.append(address)
            self.source_maps[source] = mapping
            self.instruction_addresses.extend(instructions)
            line_addresses = sorted((line, address) for address, line in mapping.items())
            stage_key = name.replace(" ", "")
            for line_number, text in enumerate(
                    source.read_text(errors="replace").splitlines(), 1):
                label = re.match(r"\s*([._A-Za-z][\w.]*):", text)
                if not label:
                    continue
                address = next((address for line, address in line_addresses
                                if line >= line_number), None)
                if address is None:
                    continue
                label_name = label.group(1)
                self.labels[f"{stage_key}:{label_name}".lower()] = address
                self.labels[f"{source.stem}:{label_name}".lower()] = address
                self.labels_by_address.setdefault(address, f"{stage_key}:{label_name}")
        self.instruction_addresses = sorted(set(self.instruction_addresses))
        stage4 = ROOT / "boot/stage4.elf"
        if stage4.exists():
            disassembly = subprocess.check_output(
                ["objdump", "-d", "-Mintel", str(stage4)], text=True)
            for line in disassembly.splitlines():
                match = re.match(r"\s*([0-9A-Fa-f]+):\s", line)
                if match:
                    self.instruction_addresses.append(int(match.group(1), 16))
            self.instruction_addresses = sorted(set(self.instruction_addresses))

    @staticmethod
    def _is_instruction(source_line: str) -> bool:
        text = source_line.split(";", 1)[0].strip()
        if not text or text.startswith("["):
            return False
        label = re.match(r"[._A-Za-z][\w.]*:\s*(.*)", text)
        if label:
            text = label.group(1).strip()
        if not text:
            return False
        parts = text.split()
        token = parts[0].lower()
        directives = {
            "org", "equ", "db", "dw", "dd", "dq", "dt", "do", "dy", "dz",
            "resb", "resw", "resd", "resq", "times", "section", "segment",
            "global", "extern", "bits", "align",
        }
        if token in directives:
            return False
        # NASM permits data labels without a trailing colon: "name db 0".
        return len(parts) < 2 or parts[1].lower() not in directives

    def start(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory(prefix="64-dem-bios-debug-")
        self._make_source_maps()
        run_args = [str(ROOT / "bios_qemu.sh")]
        if self.args.image:
            run_args += ["--image", self.args.image]
        run_args += ["--", "-S", "-gdb", f"tcp:127.0.0.1:{self.args.port}",
                     "-accel", "tcg,thread=single",
                     "-icount", "shift=0,align=off,sleep=off"]
        if self.args.headless:
            run_args += ["-display", "none"]
        self.qemu = subprocess.Popen(
            run_args, cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        threading.Thread(target=self._read_qemu, daemon=True).start()

        deadline = time.monotonic() + 120
        while not self.image_ready.wait(0.1):
            self._check_qemu()
            if time.monotonic() >= deadline:
                raise TimeoutError("bios_qemu.sh did not finish building the disk image")
        # bios_qemu.sh prints the ready marker immediately before execing QEMU.
        # Let QEMU bind the stub before asking GDB to connect; repeated failed
        # remote sessions are surprisingly disruptive to QEMU's GDB stub.
        time.sleep(0.25)
        self.gdb = GdbMI(self.args.gdb)
        self.gdb.command("-gdb-set mi-async on")
        deadline = time.monotonic() + 15
        while True:
            self._check_qemu()
            try:
                self.gdb.command(
                    f"-target-select remote 127.0.0.1:{self.args.port}", timeout=1)
                break
            except (RuntimeError, TimeoutError):
                if time.monotonic() >= deadline:
                    raise TimeoutError("QEMU did not open its GDB server")
                time.sleep(0.1)

        self.gdb.console("set architecture i8086")
        self.gdb.console("set disassembly-flavor intel")
        self.status = "fast-forwarding BIOS firmware to 0000:7c00"
        self.gdb.console("hbreak *0x7c00")
        self.gdb.command("-exec-continue")
        self.gdb.wait_stopped(timeout=60)
        self.gdb.console("delete breakpoints")
        self.status = "paused at the first boot-sector instruction"
        self.refresh()

    def _read_qemu(self) -> None:
        assert self.qemu and self.qemu.stdout
        for line in self.qemu.stdout:
            clean = line.rstrip()
            self.log.append(clean)
            if clean.startswith("BIOS image ready:"):
                self.image_ready.set()

    def _check_qemu(self) -> None:
        if self.qemu and self.qemu.poll() is not None:
            raise RuntimeError(
                f"QEMU exited with status {self.qemu.returncode}\n" + "\n".join(self.log))

    @staticmethod
    def _number(value: str) -> int:
        try:
            return int(value, 0)
        except ValueError:
            return 0

    def _instruction_address(self) -> int:
        cs = self._number(self.registers.get("cs", "0")) & 0xFFFF
        ip_value = self.registers.get("rip", self.registers.get("eip", "0"))
        ip = self._number(ip_value)
        if self.cpu_mode == "real mode":
            return (cs << 4) + (ip & 0xFFFF)
        # This bootloader uses flat protected/long-mode code segments. Once
        # paging is enabled GDB also interprets this as a virtual address.
        return ip

    def _format_address(self, address: int) -> str:
        if self.cpu_mode == "real mode":
            cs = self._number(self.registers.get("cs", "0")) & 0xFFFF
            offset = address - (cs << 4)
            if 0 <= offset <= 0xFFFF:
                return f"{cs:04x}:{offset:04x}"
            # Use a canonical real-mode representation when browsing outside
            # the currently selected code segment.
            segment = min(address >> 4, 0xFFFF)
            offset = address - (segment << 4)
            return f"{segment:04x}:{offset:04x}"
        if self.code_bits == 64:
            return f"0x{address & 0xffffffffffffffff:016x}"
        return f"0x{address & 0xffffffff:08x}"

    def _stack_layout(self, slot_offset: int | None = None) -> tuple[int, int, int, int]:
        ss = self._number(self.registers.get("ss", "0")) & 0xFFFF
        sp_value = self.registers.get("rsp", self.registers.get("esp", "0"))
        sp = self._number(sp_value)
        slot_size = 8 if self.code_bits == 64 else (4 if self.code_bits == 32 else 2)
        byte_offset = (self.stack_offset if slot_offset is None else slot_offset) * slot_size
        if self.cpu_mode == "real mode":
            displayed_offset = ((sp & 0xFFFF) + byte_offset) & 0xFFFF
            return (ss << 4) + displayed_offset, displayed_offset, slot_size, ss
        mask = 0xFFFFFFFFFFFFFFFF if slot_size == 8 else 0xFFFFFFFF
        displayed_offset = (sp + byte_offset) & mask
        return displayed_offset, displayed_offset, slot_size, ss

    def _stack_value_text(self, slot: str, value: int, digits: int, cs: int) -> str:
        target = ((cs << 4) + (value & 0xFFFF)
                  if self.cpu_mode == "real mode" else value)
        label = self._describe_code_address(target)
        annotation = f" -> {label}" if label else ""
        return f"{slot} {value:0{digits}x}{annotation}"

    def refresh_stack(self, update_pointer: bool = True) -> None:
        """Show raw stack slots and annotate values that resemble return addresses."""
        if not self.gdb or not self.registers:
            return
        address, displayed_offset, slot_size, ss = self._stack_layout()
        self.stack_register = "RSP" if slot_size == 8 else ("ESP" if slot_size == 4 else "SP")
        try:
            data = self._read_code(address, slot_size * 16)
            lines = []
            digits = slot_size * 2
            cs = self._number(self.registers.get("cs", "0")) & 0xFFFF
            if update_pointer or not self.stack_pointer_line:
                pointer_address, pointer_offset, _, pointer_ss = self._stack_layout(0)
                pointer_data = (data[:slot_size] if self.stack_offset == 0
                                else self._read_code(pointer_address, slot_size))
                pointer_value = int.from_bytes(pointer_data, "little")
                pointer_slot = (f"{pointer_ss:04x}:{pointer_offset:04x}"
                                if self.cpu_mode == "real mode"
                                else self._format_address(pointer_offset))
                self.stack_pointer_line = self._stack_value_text(
                    pointer_slot, pointer_value, digits, cs)
            for index in range(min(16, len(data) // slot_size)):
                start = index * slot_size
                value = int.from_bytes(data[start:start + slot_size], "little")
                if self.cpu_mode == "real mode":
                    slot = f"{ss:04x}:{(displayed_offset + start) & 0xffff:04x}"
                else:
                    mask = 0xFFFFFFFFFFFFFFFF if slot_size == 8 else 0xFFFFFFFF
                    slot = self._format_address((displayed_offset + start) & mask)
                marker = "->" if index == 0 else "  "
                lines.append(f"   {marker} " + self._stack_value_text(
                    slot, value, digits, cs))
            if self.stack_offset == 0 and lines:
                lines[0] = f"{self.stack_register}>-> " + self._stack_value_text(
                    (f"{ss:04x}:{displayed_offset:04x}" if self.cpu_mode == "real mode"
                     else self._format_address(displayed_offset)),
                    int.from_bytes(data[:slot_size], "little"), digits, cs)
                self.stack_lines = lines
            else:
                self.stack_lines = [f"{self.stack_register}>   {self.stack_pointer_line}", "", *lines]
        except RuntimeError as error:
            self.stack_lines = [f"Stack unavailable: {error}"]

    def _describe_code_address(self, address: int) -> str | None:
        index = bisect.bisect_left(self.instruction_addresses, address)
        if (index >= len(self.instruction_addresses)
                or self.instruction_addresses[index] != address):
            return None
        label_addresses = sorted(self.labels_by_address)
        label_index = bisect.bisect_right(label_addresses, address) - 1
        if label_index < 0:
            return self._format_address(address)
        label_address = label_addresses[label_index]
        # Do not attribute an address to a label in a different loaded stage.
        if address - label_address > 0x400:
            return self._format_address(address)
        label = self.labels_by_address[label_address]
        difference = address - label_address
        return label if difference == 0 else f"{label}+0x{difference:x}"

    def _detect_cpu_mode(self, cr0: int) -> tuple[str, int]:
        assert self.gdb
        try:
            qemu_registers = self.gdb.console("monitor info registers")
            cs_line = next((line for line in qemu_registers.splitlines()
                            if line.lstrip().startswith("CS =")), "")
            if "CS64" in cs_line:
                return "long mode", 64
            if "CS32" in cs_line:
                return "protected mode", 32
            if cr0 & 1:
                return "protected mode (16-bit code)", 16
        except (RuntimeError, TimeoutError):
            efer = self._number(self.registers.get("efer", "0"))
            if efer & (1 << 10):
                return "long mode", 64
            if cr0 & 1:
                return "protected mode", 32
        return "real mode", 16

    def _read_code(self, address: int, length: int = 64) -> bytes:
        assert self.gdb
        records = self.gdb.command(
            f"-data-read-memory-bytes 0x{address:x} {length}")
        for record in records:
            match = re.search(r'contents="([0-9a-fA-F]+)"', record)
            if match:
                return bytes.fromhex(match.group(1))
        raise RuntimeError("GDB did not return instruction bytes")

    def _read_code_cached(self, address: int, length: int) -> bytes:
        # Adjacent arrow-key views normally share this block, avoiding a GDB
        # round trip and making held-key browsing feel immediate.
        base = address & ~0xFF
        block = self.memory_cache.get(base)
        if block is None:
            try:
                block = self._read_code(base, 768)
                self.memory_cache[base] = block
            except RuntimeError:
                return self._read_code(address, length)
        offset = address - base
        return block[offset:offset + length]

    def refresh(self, reset_view: bool = False) -> None:
        assert self.gdb
        self.previous = self.registers
        raw = self.gdb.console("info registers")
        current: dict[str, str] = {}
        for line in raw.splitlines():
            match = re.match(r"(\S+)\s+(0x[0-9a-fA-F]+|[-]?[0-9]+)", line)
            if match:
                current[match.group(1).lower()] = match.group(2)
        self.registers = current
        cr0 = self._number(current.get("cr0", "0"))
        self.paging_enabled = bool(cr0 & (1 << 31))
        self.cpu_mode, self.code_bits = self._detect_cpu_mode(cr0)
        address = self._instruction_address()
        self.pc_address = address
        if reset_view or self.view_address is None:
            self.view_address = address
        if self.cpu_mode == "real mode":
            self.location = (
                f"real mode  address={self._format_address(address)}  physical=0x{address:05x}")
        elif self.paging_enabled:
            self.location = f"{self.cpu_mode}  virtual address={self._format_address(address)}"
        else:
            width = "64-bit" if self.code_bits == 64 else "32-bit flat"
            self.location = f"{self.cpu_mode}  {width} address={self._format_address(address)}"
        self.refresh_machine_code()
        self.refresh_stack()

    def refresh_machine_code(self) -> None:
        """Refresh only the memory pane; browsing must not re-query CPU state."""
        assert self.gdb
        if self.view_address is None:
            return
        try:
            view_address = self.view_address
            known_index = bisect.bisect_left(self.instruction_addresses, view_address)
            if (known_index < len(self.instruction_addresses)
                    and self.instruction_addresses[known_index] == view_address):
                first_index = known_index
                # Show a little context above the cursor, but never cross an
                # address gap between separately loaded boot stages. Crossing
                # one made the selected stage entry disappear from the window.
                for _ in range(5):
                    if first_index == 0:
                        break
                    previous = self.instruction_addresses[first_index - 1]
                    current = self.instruction_addresses[first_index]
                    if not 0 < current - previous <= 15:
                        break
                    first_index -= 1
                decode_address = self.instruction_addresses[first_index]
            else:
                decode_address = view_address
            decoded = subprocess.run(
                ["ndisasm", "-b", str(self.code_bits), "-o", f"0x{decode_address:x}", "-"],
                input=self._read_code_cached(decode_address, 512),
                capture_output=True, check=True,
            ).stdout.decode(errors="replace")
            marked = []
            for line in decoded.splitlines():
                match = re.match(r"([0-9A-Fa-f]+)(\s+.*)", line)
                line_address = int(match.group(1), 16) if match else -1
                pc_marker = "PC>" if line_address == self.pc_address else "   "
                view_marker = "->" if line_address == view_address else "  "
                if match:
                    line = self._format_address(line_address) + match.group(2)
                marked.append(f"{pc_marker}{view_marker} {line}")
            self.disassembly = marked
        except (RuntimeError, subprocess.CalledProcessError) as error:
            self.disassembly = [f"Disassembly unavailable: {error}"]

    def step(self) -> None:
        assert self.gdb
        self.gdb.command("-exec-step-instruction")
        self.gdb.wait_stopped(timeout=10)
        self.memory_cache.clear()
        self.stack_offset = 0
        self.refresh(reset_view=True)

    def move_instruction_view(self, direction: int) -> None:
        if self.view_address is None or not self.instruction_addresses:
            return
        index = bisect.bisect_left(self.instruction_addresses, self.view_address)
        destination: int | None = None
        if direction < 0:
            previous_index = index - 1
            if previous_index >= 0:
                candidate = self.instruction_addresses[previous_index]
                # x86 instructions are at most 15 bytes. A larger distance is
                # a data/unmapped gap, so browse it byte by byte instead.
                if 0 < self.view_address - candidate <= 15:
                    destination = candidate
            if destination is None and self.view_address > 0:
                destination = self.view_address - 1
        elif index < len(self.instruction_addresses) and self.instruction_addresses[index] == self.view_address:
            next_index = index + 1
            if next_index < len(self.instruction_addresses):
                candidate = self.instruction_addresses[next_index]
                if 0 < candidate - self.view_address <= 15:
                    destination = candidate
            if destination is None:
                destination = self.view_address + 1
        else:
            # At an arbitrary memory address there is no authoritative x86
            # instruction boundary. Moving a byte keeps all memory browsable.
            destination = self.view_address + 1
        if destination is not None:
            self.view_address = destination
            self.status = f"browsing instruction memory at 0x{self.view_address:x} (CPU remains paused)"
            self.refresh_machine_code()

    def move_stack_view(self, direction: int) -> None:
        self.stack_offset = max(-4096, min(4096, self.stack_offset + direction))
        self.status = f"browsing stack at {self.stack_offset:+d} slot(s) from {self.stack_register} (CPU remains paused)"
        self.refresh_stack(update_pointer=False)

    def resolve_breakpoint(self, expression: str) -> int:
        value = expression.strip().lower()
        if value in self.labels:
            return self.labels[value]
        if ":" in value:
            segment, offset = value.split(":", 1)
            try:
                return (int(segment, 16) << 4) + int(offset, 16)
            except ValueError as error:
                raise ValueError(
                    "use an address, CS:IP, or stage:label (for example stage2:_start)") from error
        try:
            # Unprefixed addresses are hexadecimal, matching the address display.
            return int(value, 16)
        except ValueError as error:
            raise ValueError(
                "use an address, CS:IP, or stage:label (for example stage3:end_of_chain)") from error

    def run_to_breakpoint(self, expression: str) -> None:
        assert self.gdb
        address = self.resolve_breakpoint(expression)
        self.status = f"running QEMU at full speed to 0x{address:05x}"
        self.gdb.console(f"hbreak *0x{address:x}")
        try:
            self.gdb.command("-exec-continue")
            self.gdb.wait_stopped(timeout=300)
        finally:
            self.gdb.console("delete breakpoints")
        self.running = False
        self.memory_cache.clear()
        self.stack_offset = 0
        self.status = f"breakpoint hit at 0x{address:05x}"
        self.refresh(reset_view=True)

    def cleanup(self) -> None:
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
    def _box(window: curses.window, title: str) -> None:
        window.erase()
        window.box()
        try:
            window.addstr(0, 2, f" {title} ", curses.A_BOLD)
        except curses.error:
            pass

    @staticmethod
    def _lines(window: curses.window, lines: list[str], changed: set[str] | None = None) -> None:
        height, width = window.getmaxyx()
        for row, line in enumerate(lines[:height - 2], 1):
            attr = curses.A_NORMAL
            if changed and line.split(maxsplit=1)[0].lower() in changed:
                attr = curses.A_BOLD | curses.A_REVERSE
            try:
                window.addnstr(row, 1, line, width - 2, attr)
            except curses.error:
                pass
        window.noutrefresh()

    def draw(self, screen: curses.window) -> None:
        height, width = screen.getmaxyx()
        screen.erase()
        if height < 22 or width < 90:
            screen.addstr(0, 0, "Terminal must be at least 90x22. Resize it or press Q.")
            screen.refresh()
            return
        body = height - 5
        left = max(30, width // 3)
        right = width - left
        register_height = max(10, body * 2 // 3)
        registers = screen.derwin(register_height, left, 0, 0)
        stack = screen.derwin(body - register_height, left, register_height, 0)
        assembly = screen.derwin(body, right, 0, left)
        controls = screen.derwin(5, width, body, 0)
        changed = {key for key, value in self.registers.items()
                   if self.previous.get(key) != value}
        register_lines = [f"{name.upper():7} {self.registers.get(name, '—')}"
                          for name in REGISTERS if name in self.registers]
        self._box(registers, f"CPU REGISTERS — {self.cpu_mode.upper()}")
        self._lines(registers, register_lines, changed)
        self._box(stack, f"RAW STACK ({self.stack_register}) / RETURN CANDIDATES")
        self._lines(stack, self.stack_lines)
        self._box(assembly, f"FETCHED MACHINE CODE ({self.code_bits}-BIT)")
        self._lines(assembly, self.disassembly)
        self._box(controls, "CONTROL")
        state = f"RUNNING @ {self.rate:g} instr/s" if self.running else "PAUSED"
        self._lines(controls, [f"{state} | {self.status}", self.location,
                               "UP/DOWN code  W/S stack  N step  SPACE run/pause  B breakpoint  +/- speed  R live PC  Q quit"])
        curses.doupdate()

    def prompt_breakpoint(self, screen: curses.window) -> str | None:
        height, width = screen.getmaxyx()
        box_width = min(72, width - 4)
        box_height = 9
        top = max(0, (height - box_height) // 2)
        left = max(0, (width - box_width) // 2)
        dialog = screen.derwin(box_height, box_width, top, left)
        prompt = "> "
        screen.nodelay(False)
        dialog.nodelay(False)
        dialog.timeout(-1)
        dialog.keypad(True)
        value = ""
        try:
            while True:
                dialog.erase()
                dialog.box()
                dialog.addstr(0, 2, " RUN TO BREAKPOINT ", curses.A_BOLD)
                dialog.addnstr(
                    2, 2, "Enter a physical/virtual address, CS:IP, or assembly label:",
                    box_width - 4)
                dialog.addnstr(
                    3, 2, "Examples: 7d1f   0000:7d1f   stage2:_start", box_width - 4)
                dialog.addnstr(4, 2, "          stage3:end_of_chain", box_width - 4)
                dialog.addnstr(
                    6, 2, "Enter: run   Esc: cancel", box_width - 4)
                visible = value[-max(1, box_width - len(prompt) - 5):]
                dialog.addnstr(7, 2, prompt + visible, box_width - 4, curses.A_BOLD)
                dialog.refresh()
                key = dialog.getch()
                if key in (10, 13, curses.KEY_ENTER):
                    return value.strip() or None
                if key == 27:
                    return None
                if key in (curses.KEY_BACKSPACE, 8, 127):
                    value = value[:-1]
                elif 32 <= key <= 126 and len(value) < 256:
                    value += chr(key)
        except curses.error:
            return None
        finally:
            dialog.nodelay(True)
            screen.timeout(25)

    def ui(self, screen: curses.window) -> None:
        try:
            curses.curs_set(0)
        except curses.error:
            pass
        screen.keypad(True)
        screen.timeout(25)
        next_step = time.monotonic()
        dirty = True
        last_size = screen.getmaxyx()
        while not self.done:
            size = screen.getmaxyx()
            if size != last_size:
                last_size = size
                dirty = True
            if dirty:
                self.draw(screen)
                dirty = False
            key = screen.getch()
            step = False
            if key in (ord("q"), ord("Q")):
                self.done = True
            elif key == ord(" "):
                self.running = not self.running
                self.status = "running" if self.running else "paused"
                next_step = time.monotonic()
                dirty = True
            elif key in (ord("n"), ord("N")):
                self.running = False
                step = True
            elif key in (ord("b"), ord("B")):
                self.running = False
                expression = self.prompt_breakpoint(screen)
                if expression:
                    try:
                        self.run_to_breakpoint(expression)
                    except (RuntimeError, TimeoutError, ValueError) as error:
                        self.status = f"breakpoint error: {error}"
                dirty = True
            elif key == curses.KEY_UP:
                self.running = False
                self.move_instruction_view(-1)
                dirty = True
            elif key == curses.KEY_DOWN:
                self.running = False
                self.move_instruction_view(1)
                dirty = True
            elif key in (ord("w"), ord("W")):
                self.running = False
                self.move_stack_view(-1)
                dirty = True
            elif key in (ord("s"), ord("S")):
                self.running = False
                self.move_stack_view(1)
                dirty = True
            elif key in (ord("+"), ord("=")):
                self.rate = min(1000.0, self.rate * 2)
                dirty = True
            elif key in (ord("-"), ord("_")):
                self.rate = max(0.25, self.rate / 2)
                dirty = True
            elif key in (ord("r"), ord("R")):
                self.stack_offset = 0
                self.refresh(reset_view=True)
                dirty = True
            now = time.monotonic()
            if step or (self.running and now >= next_step):
                self.step()
                next_step = time.monotonic() + 1 / self.rate
                dirty = True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Single-step the 64-dem BIOS bootloader in QEMU from 0000:7c00.")
    parser.add_argument("--hz", type=float, default=5.0,
                        help="visible instruction rate (default: 5)")
    parser.add_argument("--run", action="store_true",
                        help="start running immediately after reaching the boot sector")
    parser.add_argument("--headless", action="store_true",
                        help="do not open QEMU's graphical display")
    parser.add_argument("--image", help="disk image path passed to bios_qemu.sh")
    parser.add_argument("--port", type=int, default=1235,
                        help="QEMU GDB port (default: 1235)")
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
    for program in (args.gdb, "qemu-system-x86_64", "nasm", "objdump"):
        if not shutil.which(program):
            print(f"error: required program not found: {program}", file=sys.stderr)
            return 1
    visualizer = BiosVisualizer(args)
    try:
        visualizer.start()
        curses.wrapper(visualizer.ui)
    except KeyboardInterrupt:
        pass
    except Exception as error:
        print(f"BIOS visual debugger error: {error}", file=sys.stderr)
        return 1
    finally:
        visualizer.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
