#!/usr/bin/env python3
"""Install V7 (root + /usr) onto a blank RP06 disk from the Keith Bostic tape.

Drives the simh pdp11 simulator headlessly over its telnet console: boot the
tape, then for each filesystem run the standalone mkfs and restor.  Afterwards
it writes the hpuboot boot block to block 0 so the disk boots on its own.

Usage: ./installv7.py

Run from this directory.  It creates images/v7-bostic.disk (a blank 340672-block
RP06) if missing, and needs images/v7-bostic.tap (fetch it with ../fetch
v7-keithbostic).

Two filesystems are built, matching the RP06 partition table in usr/sys/dev/hp.c:

    root  hp(0,0)      9614 blocks   (partition 0 = cylinders 0-22)
    /usr  hp(0,18392)  322278 blocks (partition 7 = cylinder 44)

The console dialogue, from the V7 "Setting Up Unix" paper (usr/doc/setup):

    : tm(0,3)                      run mkfs
    file sys size: <size>
    file system: <dev>
    : tm(0,4)                      run restor
    Tape? tm(0,5)                  (root dump)   or tm(0,6) for the /usr dump
    Disk? <dev>
    Last chance before scribbling on disk.   (return)
    End of tape

`tm` is the TU10 tape, `hp` the RP04/5/6 disk; the `(0,offset)` disk argument is
a block offset, so `hp(0,18392)` addresses the /usr partition (cylinder 44).
The tape offsets count tape files: 3 = mkfs, 4 = restor, 5 = root dump, 6 = /usr
dump.
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

# (name, filesystem size in blocks, disk "hp(unit,offset)" spec, tape dump file #)
FILESYSTEMS = [
    ('root', 9614, 'hp(0,0)', 5),
    ('/usr', 322278, 'hp(0,18392)', 6),
]


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

    def fail(msg):
        print('[driver] %s' % msg)
        snap('fail')
        print(text()[-1200:])
        proc.kill()
        return 1

    poll(3.0)
    snap('boot')

    for name, size, dev, dump in FILESYSTEMS:
        # mkfs
        send('tm(0,3)')
        if not wait_for('file sys size:'):
            return fail('%s mkfs never prompted' % name)
        send(str(size))
        if not wait_for('file system:'):
            return fail('%s mkfs never asked for the device' % name)
        send(dev)
        if not wait_for('Exit called'):
            return fail('%s mkfs did not finish' % name)
        snap('mkfs-' + name)

        # restor
        send('tm(0,4)')
        if not wait_for('Tape?'):
            return fail('%s restor never prompted for the tape' % name)
        send('tm(0,%d)' % dump)
        if not wait_for('Disk?'):
            return fail('%s restor never prompted for the disk' % name)
        send(dev)
        if not wait_for('Last chance before scribbling on disk.'):
            return fail('%s restor never warned' % name)
        send('')  # bare return past the "Last chance" prompt
        print('[driver] restoring %s dump (up to 10 min)...' % name)
        if not wait_for('End of tape', timeout=900):
            return fail('%s restor did not reach end of tape' % name)
        snap('restor-' + name)

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
