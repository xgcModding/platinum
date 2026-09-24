"""Embed the generated Mods NCGR so importing a ROM cannot replace it."""
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_bytes()
assert source[:4] == b"RGCN", "Expected an NCGR resource"
rows = [", ".join(f"0x{byte:02x}" for byte in source[i:i+16]) for i in range(0, len(source), 16)]
Path(sys.argv[2]).write_text("static const unsigned char sModLoaderIcon[] = {\n    " + ",\n    ".join(rows) + "\n};\n")
