#!/usr/bin/env python3
import os
import socket, subprocess, sys, time

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
                o=data[i+2]; rep+=bytes([IAC,DONT if c==WILL else WONT if c==DO else DONT if c==WONT else WONT,o]); i+=3
            elif c==IAC: out+=bytes([IAC]); i+=2
            elif c==SB:
                j=data.find(bytes([IAC,SE]),i); i=n if j<0 else j+2
            else: i+=2
        else: out+=bytes([b]); i+=1
    if rep:
        try: sock.sendall(rep)
        except OSError: pass
    return out

proc = subprocess.Popen([os.environ.get("VAX780", "vax780"),"tboot.ini"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=os.path.dirname(os.path.abspath(__file__)))
sock=None
for _ in range(120):
    try: sock=socket.create_connection(("127.0.0.1",10024),timeout=2); break
    except OSError: time.sleep(0.25)
sock.settimeout(0.2)
buf=bytearray()
def poll(d):
    end=time.time()+d
    while time.time()<end: buf.extend(negotiate(sock)); time.sleep(0.03)
def text(): return buf.decode('latin1','replace')
def has(s): return s in text()
def wait_for(s,t=120):
    end=time.time()+t
    while time.time()<end and not has(s): poll(0.4)
    return has(s)

poll(3.0)
if not wait_for("=", 60):
    print("NO = PROMPT"); print(text()); proc.kill(); sys.exit(1)
print("reached = prompt")

sock.sendall(b"tdcopy\r")
time.sleep(2.0); poll(1.0)
print("=== after tdcopy, first prompt ==="); print(text()[-500:])

# answer the questions (gunkies: 8 answers). Wait for ':' prompt each time.
answers = ["1","0","1","0","0","0","0","480"]
for a in answers:
    # wait for a colon prompt to appear
    wait_for(":", 30)
    sock.sendall((a+"\r").encode())
    time.sleep(0.6); poll(0.5)
    print(">>> sent answer:", a, "| last 200:", text()[-200:].replace('\n','\\n'))

# wait for the copy to finish (back at '=' prompt)
print("=== waiting for copy to complete ===")
time.sleep(15); poll(2.0)
print("=== console tail ==="); print(text()[-800:])
proc.terminate()
try: proc.wait(timeout=3)
except: proc.kill()
print("DONE")
