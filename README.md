# prebsd - fetch and run Research Unix V4-V7 and 32V on SIMH

Fetches and runs Research Unix V4, V5, V6, V7 and 32V on SIMH (the `pdp11` and `vax780` simulators), driving the console over telnet and
dropping you at (or running a command in) the single-user shell.  Started as
part of the pdp11-toolchain project, to run the *real* V7 compiler/assembler/
linker against the ported ones.

```
./prebsd ini/v7-pcollinson.ini   # boot an image (fetches the disk if missing)
./prebsd v7-pcollinson.json      # same, by a single-image JSON manifest
./fetch                          # list images (images.json)
./fetch v7-pcollinson            # download + unpack one, no boot
```

Requirements: `simh` (`pdp11` for V4-V7, `vax780` for 32V), and libcurl + json-c to
build `prebsd` (the C console driver that supersedes the Python `boot.py`).  The
tape->disk install scripts (`pdp11/installv7.py`, `vax/install32v.py`) still need
Python 3.  Fetched images (including the pre-built `v7-bostic` disk) boot straight
from `images/` with no other tooling.  The one build step — turning a tape into a
bootable disk (`pdp11/installv7.py`) — is where
[filsys](https://github.com/moebiusV/filsys) comes in: `restor` leaves the free
list broken, and `fsck.filsys -s` rebuilds it.  filsys links libfuse3 to build
`mount.filsys`, so that pulls in FUSE 3, though `fsck.filsys` itself reads and
writes the image directly and needs no FUSE.  32V needs the VAX-11/780 simulator;
the default Debian `simh` package builds only MicroVAX, so compile `vax780` from
open-simh (see `vax/README.md`).  The console is served over telnet
(`SET CONSOLE TELNET`), so nothing but the simulator binary and a raw TCP driver
is needed.

## Layout

  * `prebsd`           C driver: fetch (libcurl) + boot (telnet console)
  * `fetch`            download/unpack a disk image (see `images.json`)
  * `images.json`      the manifest: image -> files, ini, boot, mount commands
  * `ini/*.ini`        one simh config per image (device / CPU / memory / boot)
  * `boot.py`          the old Python console driver (superseded by `prebsd`)
  * `dist/`            the images, gzipped, `v4-`/`v5-`/`v6-`/`v7-`/`32v-` names
  * `vax/`             32V (VAX) tape -> disk install scripts

## The images

Most bootable disks carry the source tree on their `/usr` partition - V4, V5,
V6 (pcollinson), V7 (pcollinson and the SIMH RL02 kit), and 32V all have it;
only narukeh's V7 image is a source-less pure install.  The canonical source
distribution is the Keith Bostic V7 tape.  For every edition at once, the whole
TUHS Unix Tree is one bzipped tarball - <http://www.tuhs.org/unixtree.tar.bz2> -
rather than crawling the tree file-by-file.

| name             | system           | cc  | src | boot sequence                   |
|------------------|------------------|-----|-----|---------------------------------|
| `v4-aap`         | V4, RK05, 11/45  | yes | yes | `k` -> `unix`, `root` at `login:` |
| `v5-tuhs`        | V5, RK05, 11/45  | yes | yes | `unix` at `@`, `root` at `login:` |
| `v6-pcollinson`  | V6, RK05, 11/40  | yes | yes | `unix` at the `@` prompt        |
| `v7-pcollinson`  | V7, RP06, 11/70  | yes | yes | `boot` -> `hp(0,0)unix`          |
| `v7-rl`          | V7, RL02, 11/45  | yes | yes | `rl(0,0)rl2unix`                |
| `v7-narukeh`     | V7, RP06         | yes | no  | `boot` -> `hp(0,0)unix`          |
| `v7-keithbostic` | V7 tape, install | yes | yes | install (gunkies guide)         |
| `v7-bostic`      | V7, RP06, 11/70  | yes | yes | `boot` -> `hp(0,0)hptmunix`      |
| `32v`            | 32V tape, install| yes | yes | install (`vax/install32v.py`)    |
| `32v-disk`       | 32V, RP06, VAX   | yes | yes | —                                |

## Notes

* simh must not run while the disk is mounted by `filsys`.  Stage files with
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
| V4 (RK05 + tape) | Angelo Papenhoff (from the CHM-recovered 1973 Utah tape) | <http://squoze.net/UNIX/v4/> (`disk.rk`, `unix_v4.tap`) | <http://squoze.net/UNIX/v4/README> |
| V5 (RK05) | Dennis Ritchie / TUHS | <https://www.tuhs.org/Archive/Distributions/Research/Dennis_v5/v5root.gz> | <https://www.tuhs.org/Archive/Distributions/Research/Dennis_v5/> |
| V6 (RK05) | Peter Collinson | <https://github.com/pcollinson/unixv6-extras> (`simh/rk0.gz`, `rk1.gz`, `rk2.gz`) | <https://github.com/pcollinson/unixv6-extras> |
| V7 (RP06) | Peter Collinson | <https://github.com/pcollinson/unixv7-extras> (`bootstrap/rp06-0.disk.gz`) | <https://github.com/pcollinson/unixv7-extras> |
| V7 (RL02) | Bob Supnik | <http://simh.trailing-edge.com/kits/uv7swre.zip> | <http://simh.trailing-edge.com/> |
| V7 (pure install) | narukeh | <https://github.com/narukeh/research_unix_v7> (`rp06-0.disk.xz`) | <https://github.com/narukeh/research_unix_v7> |
| V7 (tape) | Keith Bostic / gunkies | gunkies install guide | <https://gunkies.org/wiki/Installing_v7_on_SIMH> |
| 32V (tape) | TUHS / Caldera | 32V distribution tape | <https://www.tuhs.org/Archive/> |

The 32V disk images (`32v-rp06.disk`, `32v-root.disk`) and the V7 disk image
(`v7-bostic.disk`, built by `pdp11/installv7.py` from the Keith Bostic tape)
were built by this project and are distributed directly from `dist/`.  Both are
also listed in `images.json` — `v7-bostic` and `32v-disk` (`./fetch` grabs the
gzip straight from `dist/`) — so they boot without running the install.

## Mounting (mount.filsys)

Mounting is done by [filsys](https://github.com/moebiusV/filsys), a FUSE driver
for V4-V7 and 32V filesystem images.  The RP06 images (V7 `v7-rp06.disk`, 32V
`32v-rp06.disk`) share one partition layout: root at block 0, swap at 5000,
`/usr` at 18392 (byte offset 9416704).

    # V7 (v7-rp06.disk)
    mount.filsys -v 7  v7-rp06.disk mnt
    mount.filsys -v 7  -o offset=9416704 v7-rp06.disk mnt/usr

    # 32V (32v-rp06.disk)
    mount.filsys -v 32 32v-rp06.disk mnt
    mount.filsys -v 32 -o offset=9416704 32v-rp06.disk mnt/usr

    # single-filesystem images
    mount.filsys -v 4  v4-rk05.disk mnt        # V4 root
    mount.filsys -v 5  v5-root.disk mnt        # V5 root
    mount.filsys -v 6  v6-rk0 mnt              # V6 root
    mount.filsys -v 32 32v-root.disk mnt       # 32V root only

Mount the root first, then nest the `/usr` mount on top.
