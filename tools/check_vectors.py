#!/usr/bin/env python3
"""check_vectors.py - does the linked vector table match the source and the map?

Static half of the vector-table claim (the runtime half is `vec:` over serial).
Reads the ELF with arm-none-eabi-objdump/nm/readelf and asserts:

  * .isr_vector is the first thing in flash, at 0x08000000
  * word 0 == _estack == top of the 20 KiB SRAM (0x20005000)
  * word 1 == Reset_Handler with bit 0 set (Thumb), and the ELF entry point agrees
  * words 2..15 name the handlers startup.s lists, in the architecture's order
  * reserved slots (7, 8, 9, 10, 13) are zero
  * every handler address is odd (EPSR.T would be 0 otherwise: INVSTATE)

--self-test proves the checker CAN fail: it runs the same assertions on a
doctored copy of a good table (Thumb bit cleared on entry 1, reserved slot
non-zero, stack top off by one word) and requires each to be rejected.
A checker never seen failing is decoration (04/05's rule).

usage: check_vectors.py build/firmware.elf [--cross=arm-none-eabi-]
       check_vectors.py --self-test
"""
import re
import struct
import subprocess
import sys

FLASH_BASE = 0x08000000
RAM_TOP = 0x20005000

# ARMv7-M ARM Table B1-4 / PM0056 Table 15 - system exceptions, entries 0..15
EXPECTED = [
    ("_estack", "sp"),
    ("Reset_Handler", "fn"),
    ("NMI_Handler", "fn"),
    ("HardFault_Handler", "fn"),
    ("MemManage_Handler", "fn"),
    ("BusFault_Handler", "fn"),
    ("UsageFault_Handler", "fn"),
    (None, "zero"), (None, "zero"), (None, "zero"), (None, "zero"),
    ("SVC_Handler", "fn"),
    ("DebugMon_Handler", "fn"),
    (None, "zero"),
    ("PendSV_Handler", "fn"),
    ("SysTick_Handler", "fn"),
]


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def read_table(elf, cross):
    """Return (section_address, [32-bit words]) of .isr_vector."""
    out = run([cross + "objdump", "-s", "-j", ".isr_vector", elf])
    words = []
    addr = None
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+)\s+((?:[0-9a-f]{8}\s*){1,4})", line)
        if not m:
            continue
        if addr is None:
            addr = int(m.group(1), 16)
        for hexword in m.group(2).split():
            # objdump prints the bytes in memory order; the table is little-endian
            words.append(struct.unpack("<I", bytes.fromhex(hexword))[0])
    if addr is None:
        raise SystemExit("no .isr_vector section in " + elf)
    return addr, words


def read_symbols(elf, cross):
    syms = {}
    for line in run([cross + "nm", elf]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def read_entry(elf, cross):
    out = run([cross + "readelf", "-h", elf])
    m = re.search(r"Entry point address:\s+0x([0-9a-f]+)", out)
    return int(m.group(1), 16)


def check(addr, words, syms, entry):
    """Return a list of failure strings (empty == pass)."""
    fails = []
    if addr != FLASH_BASE:
        fails.append(f".isr_vector at {addr:#010x}, must be {FLASH_BASE:#010x}")
    if len(words) < 16:
        fails.append(f"table has {len(words)} words, need at least 16")
        return fails
    for i, (name, kind) in enumerate(EXPECTED):
        w = words[i]
        if kind == "sp":
            if w != syms.get("_estack"):
                fails.append(f"entry[{i}] {w:#010x} != _estack {syms.get('_estack', 0):#010x}")
            if w != RAM_TOP:
                fails.append(f"entry[{i}] {w:#010x} is not the top of SRAM {RAM_TOP:#010x}")
        elif kind == "zero":
            if w != 0:
                fails.append(f"entry[{i}] reserved slot is {w:#010x}, must be 0")
        else:
            want = syms.get(name)
            if want is None:
                fails.append(f"entry[{i}] symbol {name} not in ELF")
                continue
            if w != (want | 1):
                fails.append(f"entry[{i}] {w:#010x} != {name}|1 {(want | 1):#010x}")
            if (w & 1) == 0:
                fails.append(f"entry[{i}] {name} Thumb bit clear - would INVSTATE on entry")
    if entry != (syms.get("Reset_Handler", -1) | 1):
        fails.append(f"ELF entry {entry:#010x} != Reset_Handler|1")
    return fails


def self_test():
    """The checker must reject each of these doctored tables."""
    syms = {"_estack": RAM_TOP, "Reset_Handler": 0x08000100, "NMI_Handler": 0x08000200,
            "HardFault_Handler": 0x08000210, "MemManage_Handler": 0x08000200,
            "BusFault_Handler": 0x08000200, "UsageFault_Handler": 0x08000200,
            "SVC_Handler": 0x08000200, "DebugMon_Handler": 0x08000200,
            "PendSV_Handler": 0x08000200, "SysTick_Handler": 0x08000300}
    good = [0] * 16
    for i, (name, kind) in enumerate(EXPECTED):
        good[i] = RAM_TOP if kind == "sp" else (0 if kind == "zero" else syms[name] | 1)
    entry = syms["Reset_Handler"] | 1

    if check(FLASH_BASE, good, syms, entry):
        print("self-test FAIL: the good table was rejected:", check(FLASH_BASE, good, syms, entry))
        return 1

    cases = {
        "thumb bit cleared on Reset_Handler": (FLASH_BASE, good[:1] + [good[1] & ~1] + good[2:], entry),
        "reserved slot 7 non-zero": (FLASH_BASE, good[:7] + [0x08000200] + good[8:], entry),
        "stack top off by one word": (FLASH_BASE, [RAM_TOP - 4] + good[1:], entry),
        "SysTick_Handler swapped for NMI": (FLASH_BASE, good[:15] + [syms["NMI_Handler"] | 1], entry),
        "table not at flash base": (FLASH_BASE + 0x100, good, entry),
        "ELF entry not Reset_Handler": (FLASH_BASE, good, syms["NMI_Handler"] | 1),
    }
    bad = 0
    for label, (a, w, e) in cases.items():
        fails = check(a, w, syms, e)
        if fails:
            print(f"self-test: rejected as designed - {label}: {fails[0]}")
        else:
            print(f"self-test FAIL: NOT rejected - {label}")
            bad += 1
    if bad:
        return 1
    print("self-test: the checker can fail (6/6 doctored tables rejected)")
    return 0


def main(argv):
    cross = "arm-none-eabi-"
    args = []
    for a in argv:
        if a.startswith("--cross="):
            cross = a.split("=", 1)[1]
        else:
            args.append(a)
    if args == ["--self-test"]:
        return self_test()
    if len(args) != 1:
        print(__doc__)
        return 2
    elf = args[0]
    addr, words = read_table(elf, cross)
    syms = read_symbols(elf, cross)
    entry = read_entry(elf, cross)
    fails = check(addr, words, syms, entry)
    if fails:
        print(f"check_vectors: {elf}: VECTOR TABLE MISMATCH")
        for f in fails:
            print("  " + f)
        return 1
    print(f"check_vectors: {elf}: table at {addr:#010x}, {len(words)} entries, "
          f"entry[0]=_estack {words[0]:#010x}, entry[1]=Reset_Handler {words[1]:#010x}, "
          f"ELF entry agrees: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
