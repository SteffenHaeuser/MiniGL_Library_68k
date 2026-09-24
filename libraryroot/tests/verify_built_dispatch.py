"""Check the actual linked table, not just the C header's layout.

Usage: python3 tests/verify_built_dispatch.py CROSS-NM DIR
DIR contains minigl.library.debug, minigl.text.bin and minigl.disasm.txt.
Extract .text with objcopy -O binary -j .text before running this check.
"""
import pathlib
import re
import struct
import subprocess
import sys

nm, directory = sys.argv[1:]
directory = pathlib.Path(directory)
symbols = {}
for line in subprocess.check_output([nm, str(directory / "minigl.library.debug")], text=True).splitlines():
    words = line.split()
    if len(words) == 3:
        symbols[words[2]] = int(words[0], 16)
data = (directory / "minigl.text.bin").read_bytes()
table = symbols["_MiniGLDispatchTable"]
assert struct.unpack_from(">4I", data, table) == (3, 624, 2, 0), "ABI/size/Classic flags"
assert struct.unpack_from(">I", data, table + 540)[0] == symbols["_GLInterleavedArrays"], "real InterleavedArrays slot"
for slot in range(20, 624, 4):
    address = struct.unpack_from(">I", data, table + slot)[0]
    assert 0 < address < len(data), ("missing/invalid function pointer", slot, address)
disasm = (directory / "minigl.disasm.txt").read_text()
assert not re.search(r"\bjsr\s+(?:a6@\(30\)|30\(a6\))", disasm), "GCC6-style positive CreateContext vector"
negative = re.findall(r"\blea\s+a6@\(-30\),a[0-5]", disasm)
assert len(negative) == 3, ("expected all three CreateContext call paths", len(negative))
print("PASS: linked V25 Classic dispatch (151 non-null entries, real InterleavedArrays); 3 negative Warp3D context vectors.")
