# prebsd - fetch and run Research Unix V4-V7 and 32V on SIMH

Fetches and runs Research Unix V4, V5, V6, V7 and 32V on SIMH (the `pdp11`
simulator for V4-V7, `vax780` for 32V), driving the console over telnet and
dropping you at (or running a command in) the single-user shell.  Built for the
Koitix toolchain project, to run the *real* V7 compiler/assembler/linker against
the ported ones.

```
./fetch                         # list images
./fetch v7-pcollinson           # download + unpack one
./boot.py --list                # numbered list of bootable images
./boot.py --img 1               # boot the 1st image from --list
./boot.py --img 1 --cmd 'cc -S /tmp/x.c'   # boot + run a command
./boot.py --ini ini/v7-pcollinson.ini       # or name the ini directly
```

Requirements: `simh` (`pdp11` for V4-V7, `vax780` for 32V), `curl`, and Python 3.
The console is served over telnet (`SET CONSOLE TELNET`), so nothing but the
simulator binary and a raw TCP driver is needed.

## Layout

  * `fetch`            download/unpack a disk image (see `images.tsv`)
  * `images.tsv`       the manifest: image -> URL, ini, boot sequence, flags
  * `ini/*.ini`        one simh config per image (device / CPU / memory / boot)
  * `boot.py`          the Python console driver (C rewrite planned)

## The images

No prebuilt bootable disk ships with the Unix source tree on it - the compiler
(`cc`, `c0`, `c1`, `c2`, `as`, `ld`) is on the bootable images, but `/usr/src`
is distributed separately (the Keith Bostic V7 tape, or the pcollinson /
narukeh git trees).  So "compiler + sources" is a *pair*: boot a disk for the
compiler, and pull the source tree off the tape or a git mirror.

| name             | system           | cc  | src | boot sequence             |
|------------------|------------------|-----|-----|---------------------------|
| `v7-pcollinson`  | V7, RP06, 11/70  | yes | no  | `boot` -> `hp(0,0)unix`    |
| `v7-rl`          | V7, RL02, 11/45  | yes | no  | `rl(0,0)rl2unix`          |
| `v7-narukeh`     | V7, RP06         | yes | no  | `boot` -> `hp(0,0)unix`    |
| `v6-pcollinson`  | V6, RK05, 11/40  | yes | no  | `unix` at the `@` prompt  |
| `v7-keithbostic` | V7 tape, install | yes | yes | install (gunkies guide)   |

## Notes

* simh must not run while the disk is mounted by `kenfs`.  Stage files with
  FUSE, unmount, then boot.
* V7's KL11 console driver hard-codes `LCASE` (`usr/sys/dev/kl.c`), assuming a
  Model 33 Teletype - it uppercases output and lowercases typed input, so `cc
  -S` arrives as `cc -s` (strip) and produces no `.s`.  `boot.py` sends
  `stty -lcase` after boot by default, restoring mixed case.  The permanent fix
  would be a kernel rebuild without `LCASE` in `kl.c`.
* The `.ini` files enable `SET CONSOLE TELNET=10023`; `boot.py` answers the
  telnet IAC negotiation so option bytes don't leak into the output.

## Sources

Where each image came from, who made it, and the page it was fetched from:

| image | originator | download link | original page |
|-------|-----------|---------------|---------------|
| V6 (RK05) | Peter Collinson | <https://github.com/pcollinson/unixv6-extras> (`simh/rk0.gz`, `rk1.gz`, `rk2.gz`) | <https://github.com/pcollinson/unixv6-extras> |
| V7 (RP06) | Peter Collinson | <https://github.com/pcollinson/unixv7-extras> (`bootstrap/rp06-0.disk.gz`) | <https://github.com/pcollinson/unixv7-extras> |
| V7 (RL02) | Bob Supnik | <http://simh.trailing-edge.com/kits/uv7swre.zip> | <http://simh.trailing-edge.com/> |
| V7 (pure install) | narukeh | <https://github.com/narukeh/research_unix_v7> (`rp06-0.disk.xz`) | <https://github.com/narukeh/research_unix_v7> |
| V7 (tape) | Keith Bostic / gunkies | gunkies install guide | <https://gunkies.org/wiki/Installing_v7_on_SIMH> |
| 32V (VAX) | TUHS / Caldera | 32V distribution tape | <https://www.tuhs.org/Archive/> |
