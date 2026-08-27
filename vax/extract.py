import struct, sys
data = open('starunix/32v/32v.tape','rb').read()
pos = 0; files = []; cur = []
while pos + 4 <= len(data):
    L = struct.unpack('<I', data[pos:pos+4])[0]; pos += 4
    if L == 0:
        if cur: files.append(cur); cur = []
        if len(files) >= 1 and all(r[0] == 0 for r in []): pass
        continue
    rec = data[pos:pos+L]; pos += L
    pos += 4  # trailing length word
    cur.append((L, rec))
if cur: files.append(cur)

print("tape files found:", len(files))
for i, f in enumerate(files):
    sizes = set(r[0] for r in f)
    print("  file %d: %d records, block sizes %s, total %d bytes" %
          (i, len(f), sorted(sizes), sum(r[0] for r in f)))

# file 1 should be the root filesystem (10240-byte records = 20 x 512 blocks)
if len(files) >= 2:
    root = files[1]
    blocks = b''.join(r[1] for r in root)
    # each 10240-byte record is 20 x 512-byte disk blocks; total should be 9600 blocks
    open('root32v.disk','wb').write(blocks)
    print("wrote root32v.disk: %d bytes = %d blocks" % (len(blocks), len(blocks)//512))
