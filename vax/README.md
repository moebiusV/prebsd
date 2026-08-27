# 32V (VAX) install

Installs 32V (the VAX port of Seventh Edition Unix) from a tape image onto an
RP06 disk image, driven headlessly over the VAX-11/780 simulator's telnet
console.

## Prerequisites

32V needs a VAX-11/780 simulator.  The `vax` binary shipped by some distros is
MicroVAX-only (no 780, no RP06/TE16), so build the 780 from open-simh:

    git clone https://github.com/open-simh/simh.git
    make vax780        # needs the vmb.exe ROM; see the simh docs

Put `vax780` on your PATH, or set `VAX780=/path/to/vax780`.

## Images

Three files, all redistributable under the Caldera Ancient UNIX License:

    32v-rp06.disk       full 340671-block RP06 image (root + swap + /usr)
    32v-tape        the 32V install tape (SIMH tape format)
    32v-root.disk    the extracted root filesystem (9600 blocks)

## Scripts

    extract.py      pull 32v-root.disk (tape file 1) out of 32v-tape
    tdcopy.py       boot tboot.ini, run tdcopy, answer its questions
    install32v.py   boot dboot.ini, mkfs /usr, tar the tape into /usr
    vaxdrive.py     reusable VAX console driver (wait_for / send / poll)

    dboot.ini       disk boot: RP06 at rp0, TE16 tape at tu0, console 10024
    tboot.ini       tape boot: same devices, boots the tape

The console is on telnet port 10024.  Each script looks for the ini and image
files in its own directory (`vax/`), so run them from here.
