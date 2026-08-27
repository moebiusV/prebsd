#!/usr/bin/env python3
"""Drive a SIMH vax780 console over telnet: wait for a prompt, send a scripted
dialogue, report what arrives."""
import os
import socket, subprocess, sys, time, os

IAC,WILL,WONT,DO,DONT,SB,SE = 0xFF,0xFB,0xFC,0xFD,0xFE,0xFA,0xF0

def negotiate(sock):
    try: data = sock.recv(65536)
    except (socket.timeout, OSError): return b''
    out=b''; rep=b''; i=0; n=len(data)
    while i<n:
        b=data[i]
        if b==IAC:
            if i+1>=n: break
            c=data[i+1]
            if c in (WILL,WONT,DO,DONT):
                if i+2>=n: break
                o=data[i+2]
                rep += bytes([IAC, DONT if c==WILL else WONT if c==DO else DONT if c==WONT else WONT, o])
                i+=3
            elif c==IAC: out+=bytes([IAC]); i+=2
            elif c==SB:
                j=data.find(bytes([IAC,SE]), i); i=n if j<0 else j+2
            else: i+=2
        else: out+=bytes([b]); i+=1
    if rep:
        try: sock.sendall(rep)
        except OSError: pass
    return out

class Vax:
    def __init__(self, ini, port=10024):
        self.port = port
        self.proc = subprocess.Popen([os.environ.get("VAX780", "vax780"), ini],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.STDOUT, cwd=os.path.dirname(os.path.abspath(__file__)))
        self.sock=None
        for _ in range(120):
            try:
                self.sock=socket.create_connection(("127.0.0.1",port), timeout=2); break
            except OSError: time.sleep(0.25)
        if not self.sock:
            print("[driver] connect failed"); self.proc.kill(); sys.exit(1)
        self.sock.settimeout(0.2)
        self.buf=bytearray()
    def poll(self, d=0.5):
        end=time.time()+d
        while time.time()<end:
            self.buf.extend(negotiate(self.sock)); time.sleep(0.03)
    def text(self): return self.buf.decode('latin1', 'replace')
    def has(self, s): return s in self.text()
    def wait_for(self, s, timeout=120):
        end=time.time()+timeout
        while time.time()<end and not self.has(s): self.poll(0.4)
        return self.has(s)
    def send(self, s):
        self.sock.sendall((s+"\r").encode())
    def close(self):
        try: self.sock.close()
        except: pass
        self.proc.terminate()
        try: self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired: self.proc.kill()

if __name__=="__main__":
    ini = sys.argv[1]
    v = Vax(ini)
    v.poll(3.0)
    print("=== console after boot ===")
    print(v.text())
    # wait for '=' tape prompt
    if v.wait_for("=", 60):
        print("=== reached '=' prompt ===")
    else:
        print("=== no '=' prompt seen ===")
    v.close()
