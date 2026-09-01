# V7 (Keith Bostic tape) install

Builds a complete bootable Seventh Edition Unix system (root **and** `/usr`)
on a blank RP06 disk image from the Keith Bostic distribution tape
(`v7-bostic.tap`), driven headlessly over the simh pdp11 telnet console.  This
mirrors the 32V VAX install in `../vax/`.

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
    ./installv7.py              # mkfs + restor (root + /usr) + boot block

The dialogue, from the V7 "Setting Up Unix" paper (`usr/doc/setup`), is run
once per filesystem (`tm` is the TU10 tape, `hp` the RP04/5/6 disk):

    : tm(0,3)                     run mkfs
    file sys size: <size>
    file system: <dev>
    : tm(0,4)                     run restor
    Tape? tm(0,5|6)
    Disk? <dev>
    Last chance before scribbling on disk.   (return)
    End of tape

The two filesystems, matching the RP06 partition table in `usr/sys/dev/hp.c`:

| fs    | size (blocks) | disk arg      | tape dump |
|-------|---------------|---------------|-----------|
| root  | 9614          | `hp(0,0)`     | `tm(0,5)` |
| /usr  | 322278        | `hp(0,18392)` | `tm(0,6)` |

The root filesystem is **9614 blocks** (partition 0, cylinders 0-22).  `/usr` is
addressed by its block offset (`hp(0,18392)` = cylinder 44, partition 7).
The standalone `mkfs` inode density reproduces the original `/usr` superblock
exactly (`isize = 8189`).

After the restore, `installv7.py` writes the `hpuboot` boot block to block 0.
Mount with the `filsys` tools:

    filsysmount -v 7 v7-bostic.disk mnt
    filsysmount -v 7 -o offset=9416704 v7-bostic.disk mnt/usr

## Free list repair after restore

`restor` does not rebuild the free list: it leaves `s_tfree` intact, but the
chained free-block dump blocks are gone, so the disk can't allocate new blocks.
The V7 fix is `icheck -s` (the `restor(1m)` page says it "must be done"), but
`icheck`/`dcheck` aren't loadable from the tape — only `mkfs` and `restor` are —
and they must run on a dismounted filesystem.  On the root, which can't be
dismounted, run `icheck -s`, `sync`, then reboot immediately so the kernel
doesn't write back its stale superblock.

`fsck.filsys` does the same repair on the host:

    fsck.filsys -v 7 v7-bostic.disk      # check (reports the broken free list)
    fsck.filsys -v 7 -s v7-bostic.disk   # rebuild the free list (icheck -s)
    fsck.filsys -v 7 -r v7-bostic.disk   # resolve duplicate blocks (salv -a)

It covers the V7 repair toolkit: `icheck` (blocks/inodes), `dcheck` (link
counts), `ncheck` (inode -> pathname, `-n ino`), `clri` (clear an inode,
`-c ino`); `-s` is `icheck -s`, `-r` is `salv -a`.

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
