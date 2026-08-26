# runv7 — boot Research Unix V6/V7 on simh, headlessly

Fetches a V6 or V7 disk image, boots it under simh with the console driven over
telnet, and drops you at (or runs a command in) the single-user shell.  Built for
the Koitix toolchain project, to run the *real* V7 compiler/assembler/linker
against the ported ones.

```
./fetch                         # list images
./fetch v7-pcollinson           # download + unpack one
./boot.py --ini ini/v7-pcollinson.ini
./boot.py --ini ini/v7-pcollinson.ini --cmd 'cc -S /tmp/x.c'
```

Requirements: `simh` (`pdp11` on the PATH), `curl`, and Python 3.  The console
is served over telnet (`SET CONSOLE TELNET`), so nothing but the pdp11 binary and
a raw TCP driver is needed.

## Layout

  * `fetch`            download/unpack a disk image (see `images.tsv`)
  * `images.tsv`       the manifest: image → URL, ini, boot sequence, flags
  * `ini/*.ini`        one simh config per image (device / CPU / memory / boot)
  * `boot.py`          the Python console driver (C rewrite planned)

## The images

No prebuilt bootable disk ships with the Unix source tree on it — the compiler
(`cc`, `c0`, `c1`, `c2`, `as`, `ld`) is on the bootable images, but `/usr/src`
is distributed separately (the Keith Bostic V7 tape, or the pcollinson /
narukeh git trees).  So "compiler + sources" is a *pair*: boot a disk for the
compiler, and pull the source tree off the tape or a git mirror.

| name             | system           | cc  | src | boot sequence             |
|------------------|------------------|-----|-----|---------------------------|
| `v7-pcollinson`  | V7, RP06, 11/70  | yes | no  | `boot` → `hp(0,0)unix`    |
| `v7-rl`          | V7, RL02, 11/45  | yes | no  | `rl(0,0)rl2unix`          |
| `v7-narukeh`     | V7, RP06         | yes | no  | `boot` → `hp(0,0)unix`    |
| `v6-pcollinson`  | V6, RK05, 11/40  | yes | no  | `unix` at the `@` prompt  |
| `v7-keithbostic` | V7 tape, install | yes | yes | install (gunkies guide)   |

## Notes

* simh must not run while the disk is mounted by `v7fuse` (see that project's
  flock coordination).  Stage files with FUSE, unmount, then boot.
* V7's console is in KSR uppercase mode, so `cat` of a lowercase file shows
  uppercase; dump bytes with `od -b` for exact content.
* The `.ini` files enable `SET CONSOLE TELNET=10023`; `boot.py` answers the
  telnet IAC negotiation so option bytes don't leak into the output.

## Sources

* <https://github.com/pcollinson/unixv7-extras> (V7 RP06 image)
* <https://github.com/pcollinson/unixv6-extras> (V6 RK05 images)
* <http://simh.trailing-edge.com/kits/uv7swre.zip> (V7 RL02 image)
* <https://github.com/narukeh/research_unix_v7> (V7 RP06 pure install)
* <https://gunkies.org/wiki/Installing_v7_on_SIMH> (Keith Bostic V7 tape install)
