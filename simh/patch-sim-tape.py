#!/usr/bin/env python3
"""patch-sim-tape.py SIM_TAPE.C: make the simulator's tape seek drop stdio's buffered data.

Without it, a tape file that the host rewrites while the simulator has it attached goes on
being served from the first block read (see docs/ORACLE.md).  Works on the CRLF source of
open-simh and the LF source of Richard Cornwell's tree; already patched is fine."""
import sys

p = sys.argv[1]
s = open(p, "rb").read()
if b"imp72 oracle" in s:
    sys.exit(0)
for nl in (b"\r\n", b"\n"):
    old = (b"if (MT_GET_FMT (uptr) < MTUF_F_ANSI)" + nl + b"    return sim_fseek (uptr->fileref, pos, SEEK_SET);" + nl + b"return 0;")
    new = (b"if (MT_GET_FMT (uptr) < MTUF_F_ANSI) {" + nl +
           b"    fflush (uptr->fileref);                             /* imp72 oracle: no stale buffered data after the file was rewritten */" + nl +
           b"    return sim_fseek (uptr->fileref, pos, SEEK_SET);" + nl + b"    }" + nl + b"return 0;")
    if old in s:
        open(p, "wb").write(s.replace(old, new, 1))
        sys.exit(0)
sys.exit("sim_tape_seek not found in " + p)
