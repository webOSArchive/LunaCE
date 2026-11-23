#!/usr/bin/env python3
"""Compare two LunaSysMgr builds of the same source, function by function.

    cross-build/compare-builds.py SHIPPED CLEAN

Use it to check a shipped binary (bin/LunaSysMgr-LunaCE-*) against a clean build of
the same commit, made in a separate worktree. If the clean build is not
byte-identical, the differences must be ones a clean build can cause: embedded
source paths change length, so data moves and the literal-pool words that hold its
addresses change. Every instruction must still match. If a function's instructions
differ, an object in the shipped build was compiled against old headers. That is how
webOS-Community-Edition issue #16 shipped: CardWebApp.o read Settings fields at
offsets from before a field was inserted.

Exit status: 0 if every instruction matches, 1 if any function differs.
"""
import re
import subprocess
import sys
from collections import defaultdict

TOOLS = "/opt/PalmPDK/arm-gcc/bin/arm-none-linux-gnueabi-"
FUNC = re.compile(r"^[0-9a-f]+ <(.+)>:$")
INSN = re.compile(r"^\s*[0-9a-f]+:\s+([0-9a-f]+)\s+(.*)$")


def functions(path):
    """{function: [(raw word, instruction text)]} for .text, without addresses."""
    out = subprocess.run([TOOLS + "objdump", "-d", "-C", "-j", ".text", path],
                         check=True, capture_output=True, text=True).stdout
    funcs, cur = defaultdict(list), None
    for line in out.splitlines():
        m = FUNC.match(line)
        if m:
            cur = m.group(1)
            continue
        m = INSN.match(line)
        if m and cur:
            # Drop objdump's "; 0x..." comment: it repeats addresses already compared.
            insn = m.group(2).split(";")[0].strip()
            # A branch names its target by address and symbol; code that moved keeps the
            # symbol, so compare that ("bl 9465c <Foo::bar()+0x10>" -> "bl <Foo::bar()+0x10>").
            insn = re.sub(r"\b[0-9a-f]+ (<[^>]+>)", r"\1", insn)
            funcs[cur].append((m.group(1), insn))
    return funcs


def sections(path):
    out = subprocess.run([TOOLS + "readelf", "-SW", path],
                         check=True, capture_output=True, text=True).stdout
    secs = {}
    for line in out.splitlines():
        m = re.search(r"\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m:
            secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return secs


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    shipped, clean = sys.argv[1], sys.argv[2]
    a, b = functions(shipped), functions(clean)
    sa, sb = sections(shipped), sections(clean)
    # The farthest an address can legitimately move: the largest section shift, plus
    # any change in .text's size (code after a stale, resized function moves too).
    shift = max(abs(sa[s][0] - sb[s][0]) for s in sa if s in sb) \
        + abs(sa[".text"][1] - sb[".text"][1]) + 0x100
    text = sa[".text"]
    if text != sb[".text"]:
        print(f"note: .text differs in placement or size: {text} vs {sb['.text']}")

    only = sorted(set(a) ^ set(b))
    bad = []
    for name in sorted(set(a) & set(b)):
        fa, fb = a[name], b[name]
        if len(fa) != len(fb):
            bad.append((name, "different length"))
            continue
        for (wa, ia), (wb, ib) in zip(fa, fb):
            if ia == ib:
                continue
            if ia.startswith(".word") and ib.startswith(".word"):
                va, vb = int(wa, 16), int(wb, 16)
                # An address (or a PC-relative offset to one) that moved with the data.
                if abs((va - vb + 2**31) % 2**32 - 2**31) <= shift:
                    continue
            bad.append((name, f"{ia}  vs  {ib}"))
            break

    for name in only:
        print(f"ONLY IN ONE BUILD: {name}")
    for name, why in bad:
        print(f"DIFFERS: {name}: {why}")
    total = len(set(a) & set(b))
    print(f"{total} functions compared, {len(bad)} differ, {len(only)} in one build only")
    sys.exit(1 if bad or only else 0)


if __name__ == "__main__":
    main()
