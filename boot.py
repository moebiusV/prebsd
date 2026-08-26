#!/usr/bin/env python3
"""Boot a V6/V7 disk image headlessly on simh (SET CONSOLE TELNET).

The disk is described by a simh .ini (device, CPU, memory, boot command) and a
boot *sequence* — the console input needed after the .ini's `boot` line runs.
Usage:

    ./boot.py --ini ini/v7-pcollinson.ini            # boot and drop at the shell
    ./boot.py --ini ini/v7-pcollinson.ini --cmd 'cc -S /tmp/x.c'   # run a command

The boot sequence is SEND>EXPECT pairs joined by '|'.  The console is driven
over the telnet listener the .ini enables (SET CONSOLE TELNET).  A raw TCP
driver must answer telnet IAC negotiation (refuse WILL/DO) or option bytes leak
into the output.

This is the temporary Python driver; a C rewrite is planned.
"""
import argparse
import os
import select
import socket
import subprocess
import sys
import threading
import time

IAC = 0xFF
WILL, WONT, DO, DONT = 0xFB, 0xFC, 0xFD, 0xFE
SB, SE = 0xFA, 0xF0


def negotiate(sock):
    """Read pending telnet bytes, strip/answer IAC negotiation, return payload."""
    try:
        data = sock.recv(65536)
    except socket.timeout:
        return b''
    except OSError:
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
                if c == WILL:
                    rep += bytes([IAC, DONT, o])
                elif c == DO:
                    rep += bytes([IAC, WONT, o])
                elif c == WONT:
                    rep += bytes([IAC, DONT, o])
                elif c == DONT:
                    rep += bytes([IAC, WONT, o])
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
    ap = argparse.ArgumentParser()
    ap.add_argument('--ini', default='headless.ini', help='simh config file')
    ap.add_argument('--boot', default='boot>:|hp(0,0)unix>mem =',
                    help='console boot sequence SEND>EXPECT|SEND>EXPECT')
    ap.add_argument('--port', type=int, default=10023, help='telnet port')
    ap.add_argument('--cmd', default=None,
                    help='command to run after boot (captured)')
    ap.add_argument('--timeout', type=int, default=90,
                    help='max seconds to wait for each boot marker')
    args = ap.parse_args()

    simh = os.environ.get('SIMH', 'pdp11')
    ini_dir = os.path.dirname(os.path.abspath(args.ini)) or '.'
    proc = subprocess.Popen(
        [simh, os.path.basename(args.ini)],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, cwd=ini_dir,
    )

    def drain():
        while True:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if not r:
                continue
            d = os.read(proc.stdout.fileno(), 65536)
            if not d:
                return
            sys.stdout.write('[simh] ' + d.decode(errors='replace'))
            sys.stdout.flush()

    threading.Thread(target=drain, daemon=True).start()

    sock = None
    for _ in range(80):
        try:
            sock = socket.create_connection(('127.0.0.1', args.port), timeout=2)
            break
        except OSError:
            time.sleep(0.25)
    if sock is None:
        print('[driver] connect failed')
        proc.kill()
        return 1
    sock.settimeout(0.2)
    print('[driver] connected to telnet console')

    console = bytearray()

    def poll(dur):
        end = time.time() + dur
        while time.time() < end:
            console.extend(negotiate(sock))
            time.sleep(0.05)

    def has(sub):
        return sub.encode() in bytes(console)

    def wait_for(sub, timeout):
        end = time.time() + timeout
        while time.time() < end and not has(sub):
            poll(0.5)
        return has(sub)

    poll(2.0)  # settle negotiation

    steps = args.boot.split('|')
    for step in steps:
        if not step or '>' not in step:
            continue
        send, expect = step.split('>', 1)
        sock.sendall(send.encode() + b'\r')
        print('[driver] sent %r, waiting for %r' % (send, expect))
        if not wait_for(expect, args.timeout):
            print('[driver] TIMEOUT waiting for %r' % expect)
            break

    if args.cmd:
        sock.sendall((args.cmd + '\r').encode())
        print('[driver] ran: %s' % args.cmd)
        poll(6.0)

    # settle, then tear down
    poll(1.0)
    sock.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()

    txt = bytes(console).decode(errors='replace')
    print('\n=== console ===')
    print(txt)
    up = txt.upper()
    booted = ('# ' in up) or ('@' in up)
    print('=== [driver] booted to a shell prompt: %s ===' % booted)
    return 0 if booted else 1


if __name__ == '__main__':
    sys.exit(main())
