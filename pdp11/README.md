# V7 (Keith Bostic tape) install

Builds a bootable Seventh Edition Unix root filesystem on a blank RP06 disk
image from the Keith Bostic distribution tape (`v7-bostic.tap`), driven
headlessly over the simh pdp11 telnet console.  This mirrors the 32V VAX
install in `../vax/`.

## The tape

`v7-bostic.tap` is the TUHS Keith Bostic V7 distribution — a 9-track TU10
(TM11) tape.  Its layout (tape file number = the offset the boot program
skips):

| file | contents |
|------|----------|
| 0    | `mtboot` (magtape bootstrap) + the standalone `boot` program |
| 1    | `cat` (file-to-console copy) |
| 2    | `contents` |
| 3    | `mkfs` |
| 4    | `restor` |
| 5    | the root filesystem dump |
| 6    | the `/usr` filesystem dump |

## Install

    ../fetch v7-keithbostic     # downloads v7-bostic.tap into images/
    ./installv7.py              # mkfs + restor + boot block -> images/v7-bostic.disk

The dialogue, from the V7 "Setting Up Unix" paper (`usr/doc/setup`):

    : tm(0,3)                     run mkfs
    file sys size: 9614           (the full RP06 root partition)
    file system: hp(0,0)
    : tm(0,4)                     run restor
    Tape? tm(0,5)
    Disk? hp(0,0)
    Last chance before scribbling on disk.   (return)
    End of tape

`tm` is the TU10 tape, `hp` the RP04/5/6 disk.  The root filesystem must be
**9614 blocks** (the RP06 `a` partition) — the gunkies.org recipe's 5000 blocks
is too small and `restor` dies with `Out of space`.

After the restore, `installv7.py` writes the `hpuboot` boot block to block 0.

## Booting the installed disk

The disk boot is three-stage (see `../ini/v7-keithbostic.ini`):

    boot rp0          -> hpuboot (block 0) silently waits for a pathname
    boot              -> hpuboot loads /boot, the 2nd-stage boot program ("Boot:")
    hp(0,0)hptmunix   -> /boot loads the kernel -> "mem = ..." -> "#"

With boot.py the whole sequence is the `boot` column in `images.json`:
`boot>:|hp(0,0)hptmunix>mem =`.

Note the CPU split: the **install** (`tboot.ini`) must run an **11/45** — the
standalone TM11 tape driver fails under the 11/70's Unibus map (every tape read
comes back zero) — while the **kernel** (`hptmunix`) is built for the **11/70**
(RP04/5/6 on the RH70), so the disk boot (`dboot.ini`) uses an 11/70.

## Boot block provenance

`hpuboot` (the 512-byte RP06 boot block written to block 0) is byte-identical
to the original shipped in the Bostic tape's `/usr/mdec` — assembled by the
ported V7 assembler from `hpuboot.s`, then stripped the way V7's `strip` does
(`a_syms = 0`, `a_flag |= 1`).  The un-stripped source form lives in the filsys
project (`boot/hpuboot.s`).
