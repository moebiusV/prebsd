# TOPS-10 on SIMH (prebsd/pdp10)

DEC's TOPS-10 6.03 on the PDP-10 simulators of [Richard Cornwell's sims](https://github.com/rcornwell/sims)
(also in open-simh).  Not Unix, but older than BSD, and a PDP-10 with the right CPU runs
real 36 bit word code: the originals of the IMP72 compiler and of much of the early
compiler literature.

| image            | CPU  | disks            | floating point conversions |
|------------------|------|------------------|----------------------------|
| `tops10-603-ka`  | KA10 | 4 x RP03 (`dpa`) | no: `FLTR`, `FIX` are illegal UUOs |
| `tops10-603-ki`  | KI10 | 4 x RP03 (`dpa`) | yes |

```
./build-simh                          # once: simh/bin/pdp10-ka and pdp10-ki
./prebsd -k tops10-603-ka             # CTY on telnet 10024, DC lines on 2021
./prebsd -k tops10-603-ki             # the first time it makes the disks (install-ki.sh,
                                      # about twenty minutes); 10025, 2022
./prebsd -S tops10-603-ka             # stop
```

`prebsd` answers the operator's questions on the console (`SYSTEM`, the reason for the
reload, the date, the time, the start-up option) and stops when OPSER has finished its
command file (`(CP)`).  The date is typed by the boot sequence in `images.json`: 6.03
knows no year after 1999.  User terminals are the DC lines: telnet 2021 (KA10) or 2022
(KI10); log in as `[1,2]` with the password `FAILSA`, or on the KA10 as `[100,100]` with
`demo1`.

## The KI10 disks

Cornwell's pre-built system tape is restored to empty RP03 packs by his own scripts
(`restore.ini`, `refresh.do` in `tops603.zip`), which `install-ki.sh` fetches and runs.
They answer the monitor and BACKUP with `expect`/`send` and fall out of step on other
SIMH builds: use `pdp10-ki` built from Cornwell's tree (commit `be8cd4e9` works).
Everything the script makes is in `images/` (git-ignored).

## Notes

* A tape that is rewritten while the simulator has it attached is served stale: the
  simulator keeps the first block in a stdio buffer, and takes the tape's size from the
  file at attach time.  For file transfer to a running system see imp72's
  `oracle/simh-tape-fflush.patch`.
* The packs boot with `boot dpa`; on the KI10 the boot blocks are written by `WIBOOT`
  (the install script runs it).
