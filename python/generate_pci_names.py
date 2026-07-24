#!/usr/bin/env python3
"""Generate the kernel PCI name tables from a pci.ids database."""

import argparse
import json
import re
from pathlib import Path

VENDOR = re.compile(r"^([0-9a-fA-F]{4})  (.+)$")
DEVICE = re.compile(r"^\t([0-9a-fA-F]{4})  (.+)$")


def c_string(value: str) -> str:
    # C accepts \? as "?", which prevents old trigraph sequences.
    return json.dumps(value.strip(), ensure_ascii=False).replace("?", "\\?")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    vendors: list[tuple[int, str]] = []
    devices: list[tuple[int, int, str]] = []
    current_vendor: int | None = None

    for line in args.input.read_text(encoding="utf-8", errors="replace").splitlines():
        vendor_match = VENDOR.match(line)
        if vendor_match:
            current_vendor = int(vendor_match.group(1), 16)
            vendors.append((current_vendor, vendor_match.group(2)))
            continue

        device_match = DEVICE.match(line)
        if device_match and current_vendor is not None:
            devices.append((
                current_vendor,
                int(device_match.group(1), 16),
                device_match.group(2),
            ))

    vendors.sort(key=lambda entry: entry[0])
    devices.sort(key=lambda entry: (entry[0], entry[1]))

    name_offsets: dict[str, int] = {}
    names: list[str] = []
    next_offset = 0

    def name_offset(name: str) -> int:
        nonlocal next_offset
        name = name.strip()
        if name not in name_offsets:
            name_offsets[name] = next_offset
            names.append(name)
            next_offset += len(name.encode("utf-8")) + 1
        return name_offsets[name]

    for _, name in vendors:
        name_offset(name)
    for _, _, name in devices:
        name_offset(name)

    lines = [
        "/*",
        " * Generated from the PCI ID Project pci.ids database.",
        " * The source database is available under GPL-2.0-or-later or BSD-3-Clause.",
        " * Regenerate with python/generate_pci_names.py.",
        " */",
        "",
        "const char pci_name_data[] =",
    ]
    lines.extend(
        f"    {c_string(name)[:-1]}\\000\""
        for name in names
    )
    lines.extend([
        "    ;",
        "",
        "const PCI_VENDOR_NAME pci_vendor_names[] = {",
    ])
    lines.extend(
        f"    {{0x{vendor_id:04x}, {name_offset(name)}}},"
        for vendor_id, name in vendors
    )
    lines.extend([
        "};",
        "",
        "const u64 pci_vendor_name_count =",
        "    sizeof(pci_vendor_names) / sizeof(pci_vendor_names[0]);",
        "",
        "const PCI_DEVICE_NAME pci_device_names[] = {",
    ])
    lines.extend(
        f"    {{0x{vendor_id:04x}, 0x{device_id:04x}, {name_offset(name)}}},"
        for vendor_id, device_id, name in devices
    )
    lines.extend([
        "};",
        "",
        "const u64 pci_device_name_count =",
        "    sizeof(pci_device_names) / sizeof(pci_device_names[0]);",
        "",
    ])

    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
