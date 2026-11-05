"""PS2 (PCSX2 via PINE): find enemy objects by their class vtables (RTTI type_info at vtable[0]).
python tools/ps2_enemies.py [park]   -- list them; 'park' moves them 50 m below the street and holds them."""
import sys, time, struct, re
sys.path.insert(0, __import__('os').path.dirname(__file__))
import pcsx2_ref as r
d = open('analysis/disc/SLUS_213.61', 'rb').read()
e_phoff = struct.unpack_from('<I', d, 0x1c)[0]; n = struct.unpack_from('<H', d, 0x2c)[0]
segs = [struct.unpack_from('<6I', d, e_phoff + i * 32) for i in range(n)]
def va(o):
    for t, off, v, pa, fs, ms in segs:
        if t == 1 and off <= o < off + fs: return v + o - off
VT = {}
for m in re.finditer(rb'(?<=\x00)(CEm\d\d\d)\x00', d):
    s = va(m.start())
    for x in re.finditer(re.escape(struct.pack('<I', s)), d):
        if x.start() % 4: continue
        ti = struct.pack('<I', va(x.start()))
        y = d.find(ti)
        while y >= 0:
            if y % 4 == 0: VT.setdefault(va(y), m.group(1).decode())
            y = d.find(ti, y + 4)
def scan(p):
    out = []
    for base in range(0x1000000, 0x2000000, 0x10000):
        blk = p.read(base, 0x10000)
        for i in range(0, len(blk), 16):
            v = struct.unpack_from('<I', blk, i)[0]
            if v in VT: out.append((base + i, VT[v]))
    return out
if __name__ == '__main__':
    p = r.Pine(); P = 0x17E0A40
    found = scan(p)
    dx, dy, dz = struct.unpack('<3f', p.read(P + 0x50, 12))
    for a, nm in found:
        x, y, z = struct.unpack('<3f', p.read(a + 0x50, 12))
        print(hex(a), nm, round(x), round(y), round(z), 'dist', round(((x - dx) ** 2 + (z - dz) ** 2) ** .5))
    print(len(found), 'objects')
