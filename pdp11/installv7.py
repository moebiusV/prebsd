#!/usr/bin/env python3
"""Install V7 onto a blank RP06 disk from the Keith Bostic distribution tape.

Drives the simh pdp11 simulator headlessly over its telnet console: boot the
tape, run the standalone mkfs to lay out the root filesystem, then restor the
root dump.  Afterwards it writes the hpuboot boot block to block 0 so the disk
boots on its own.

Usage: ./installv7.py [fs-size]

Run from this directory.  It creates images/v7-bostic.disk (a blank 340672-block
RP06) if missing, and needs images/v7-bostic.tap (fetch it with ../fetch
v7-keithbostic).  The root filesystem defaults to 9614 blocks — the full size of
the RP06 'a' (root) partition — which the Bostic root dump requires; 5000 blocks
(the gunkies recipe) is too small and restor ends with "Out of space".

The console dialogue, from the V7 "Setting Up Unix" paper (usr/doc/setup):

    : tm(0,3)                      run mkfs
    file sys size: <fs-size>
    file system: hp(0,0)
    : tm(0,4)                      run restor
    Tape? tm(0,5)
    Disk? hp(0,0)
    Last chance before scribbling on disk.   (return)
    End of tape

`tm` is the TU10 tape (the Bostic tape), `hp` the RP04/5/6 disk.  The tape
offsets count tape files: 3 = mkfs, 4 = restor, 5 = the root dump.
"""
import os
import socket
import subprocess
import sys
import time

IAC, WILL, WONT, DO, DONT, SB, SE = 0xFF, 0xFB, 0xFC, 0xFD, 0xFE, 0xFA, 0xF0
HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.normpath(os.path.join(HERE, '..', 'images'))
DISK = os.path.join(IMAGES, 'v7-bostic.disk')
TAPE = os.path.join(IMAGES, 'v7-bostic.tap')
PORT = 10025
RP06_BLOCKS = 340672
ROOT_SIZE = int(sys.argv[1]) if len(sys.argv) > 1 else 9614


def negotiate(sock):
    try:
        data = sock.recv(65536)
    except (socket.timeout, OSError):
        return b''
    out = b''
    rep = b''
    i, n = 0, len(data)
    while i < n:
        b = data[i]
        if b == IAC:
            if i + 1 >= n:
                break
            c = data[i + 1]
            if c in (WILL, WONT, DO, DONT):
                if i + 2 >= n:
                    break
                o = data[i + 2]
                rep += bytes([IAC, DONT if c == WILL else WONT if c == DO else DONT if c == WONT else WONT, o])
                i += 3
            elif c == IAC:
                out += bytes([IAC])
                i += 2
            elif c == SB:
                j = data.find(bytes([IAC, SE]), i)
                i = n if j < 0 else j + 2
            else:
                i += 2
        else:
            out += bytes([b])
            i += 1
    if rep:
        try:
            sock.sendall(rep)
        except OSError:
            pass
    return out


def main():
    if not os.path.exists(TAPE):
        sys.exit("missing %s — run ../fetch v7-keithbostic first" % TAPE)
    if not os.path.exists(DISK):
        with open(DISK, 'wb') as f:
            f.truncate(RP06_BLOCKS * 512)

    simh = os.environ.get('SIMH', 'pdp11')
    proc = subprocess.Popen([simh, 'tboot.ini'], stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=HERE)

    sock = None
    for _ in range(120):
        try:
            sock = socket.create_connection(('127.0.0.1', PORT), timeout=2)
            break
        except OSError:
            time.sleep(0.25)
    if not sock:
        print('[driver] connect failed')
        proc.kill()
        return 1
    sock.settimeout(0.2)

    buf = bytearray()
    log = open(os.path.join(HERE, 'installv7.log'), 'w')

    def poll(d=0.5):
        end = time.time() + d
        while time.time() < end:
            buf.extend(negotiate(sock))
            time.sleep(0.03)

    def text():
        return buf.decode('latin1', 'replace')

    def has(s):
        return s in text()

    def wait_for(s, timeout=180):
        end = time.time() + timeout
        while time.time() < end and not has(s):
            poll(0.4)
        return has(s)

    def send(s):
        sock.sendall((s + '\r').encode())

    def snap(tag):
        log.write('\n=== %s ===\n%s\n' % (tag, text()[-1200:]))
        log.flush()

    poll(3.0)
    snap('boot')

    # mkfs: lay out the root filesystem on hp(0,0).
    send('tm(0,3)')
    if not wait_for('file sys size:'):
        print('[driver] mkfs never prompted'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    send(str(ROOT_SIZE))
    if not wait_for('file system:'):
        print('[driver] mkfs never asked for the device'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    send('hp(0,0)')
    if not wait_for('Exit called'):
        print('[driver] mkfs did not finish'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    snap('mkfs')

    # restor: pull the root dump off the tape onto hp(0,0).
    send('tm(0,4)')
    if not wait_for('Tape?'):
        print('[driver] restor never prompted for the tape'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    send('tm(0,5)')
    if not wait_for('Disk?'):
        print('[driver] restor never prompted for the disk'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    send('hp(0,0)')
    if not wait_for('Last chance before scribbling on disk.'):
        print('[driver] restor never warned'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    send('')  # bare return past the "Last chance" prompt
    print('[driver] restoring root dump (this takes a couple of minutes)...')
    if not wait_for('End of tape', timeout=600):
        print('[driver] restor did not reach end of tape'); snap('fail'); print(text()[-1200:]); proc.kill(); return 1
    snap('restor')

    sock.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()

    # Write the boot block (hpuboot) to block 0 so the disk boots on its own.
    with open(os.path.join(HERE, 'hpuboot'), 'rb') as f:
        boot = f.read(512)
    with open(DISK, 'r+b') as f:
        f.seek(0)
        f.write(boot)
    print('[driver] wrote hpuboot to block 0')

    print('=== done ===')
    print(text()[-800:])
    log.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
