#!/usr/bin/env python3
import os
import socket, subprocess, sys, time
IAC,WILL,WONT,DO,DONT,SB,SE=0xFF,0xFB,0xFC,0xFD,0xFE,0xFA,0xF0
def negotiate(s):
    try: d=s.recv(65536)
    except: return b''
    o=b'';r=b'';i=0;n=len(d)
    while i<n:
        b=d[i]
        if b==IAC:
            if i+1>=n: break
            c=d[i+1]
            if c in (WILL,WONT,DO,DONT):
                if i+2>=n: break
                x=d[i+2];r+=bytes([IAC,DONT if c==WILL else WONT if c==DO else DONT if c==WONT else WONT,x]);i+=3
            elif c==IAC:o+=bytes([IAC]);i+=2
            elif c==SB:
                j=d.find(bytes([IAC,SE]),i);i=n if j<0 else j+2
            else:i+=2
        else:o+=bytes([b]);i+=1
    if r:
        try:s.sendall(r)
        except:pass
    return o
proc=subprocess.Popen([os.environ.get("VAX780", "vax780"),"dboot.ini"],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT,cwd=os.path.dirname(os.path.abspath(__file__)))
sock=None
for _ in range(120):
    try:sock=socket.create_connection(("127.0.0.1",10024),timeout=2);break
    except:time.sleep(0.25)
sock.settimeout(0.2)
buf=bytearray()
def poll(d):
    e=time.time()+d
    while time.time()<e:buf.extend(negotiate(sock));time.sleep(0.03)
def t():return buf.decode('latin1','replace')
def prompt_seen(): return '#' in t() or '\xa3' in t()
def wait_prompt(tm=180):
    e=time.time()+tm
    while time.time()<e and not prompt_seen():poll(0.4)
    return prompt_seen()
def send(cmd, wait=1.0):
    sock.sendall((cmd+"\r").encode()); poll(wait)
log=open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "install32v.log"),"w")
def snap(tag): log.write("\n=== %s ===\n%s\n"%(tag,t()[-1200:])); log.flush()
poll(2.0)
for _ in range(40):
    if 'file' in t(): break
    poll(0.5)
send("unix", 8)
if not wait_prompt(180): print("NO PROMPT"); snap("fail"); print(t()[-1200:]); proc.kill(); sys.exit(1)
print("shell prompt reached", flush=True)
send("/etc/mkfs /dev/rp0h 322278", 20); snap("mkfs")
send("/etc/mount /dev/rp0h /usr", 4); snap("mount")
send("cp /dev/rmt4 /dev/null", 10)
send("cp /dev/rmt4 /dev/null", 12); snap("cp")
send("cd /usr", 2)
send("tar xvbf 20 /dev/rmt0", 2)
print("tar extracting (waiting up to 6 min)...", flush=True)
# wait for the shell prompt to reappear after tar (tar prints nothing, then prompt)
end=time.time()+360
seen_done=False
while time.time()<end:
    poll(2.0)
    # prompt back after the tar started => tar done
    if prompt_seen() and len(t())>0 and t().rstrip().endswith(('\xa3','#','\xa3 ','# ')):
        # require a quiet period: no growth for a few seconds
        l=len(t()); time.sleep(5); poll(0.5)
        if len(t())==l: seen_done=True; break
if not seen_done: print("tar may still be running; proceeding anyway", flush=True)
snap("tar-done")
send("cd /", 2)
for _ in range(4): send("sync", 4)
snap("sync")
# clean shutdown: break to SIMH, quit
sock.sendall(b"\x05")  # ctrl+E
poll(2.0)
sock.sendall(b"quit\r"); poll(2.0)
snap("quit")
print("=== final tail ==="); print(t()[-1000:])
proc.terminate()
try:proc.wait(timeout=3)
except:proc.kill()
print("DONE")
