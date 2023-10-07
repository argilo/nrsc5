#!/usr/bin/env python3

g1 = 0o133
g3 = 0o165


def parity(n):
    p = 0
    while n != 0:
        if (n & 1) == 1:
            p ^= 1
        n >>= 1
    return p


par = [parity(n) for n in range(128)]


frames = []

with open("bits-p4.txt") as f:
    while True:
        frame_str = f.read(9216)
        if len(frame_str) == 0:
            break
        frames.append([int(n) for n in frame_str])

frames = frames[4:]

identical_bits = []
for n in range(9216):
    for frame in frames[1:]:
        if frame[n] != frames[0][n]:
            break
    else:
        identical_bits.append(n)

print("identical", identical_bits)

msgs = [[int(n) for n in "011101"] for _ in range(len(frames)-64)]
rs = []
for i in range(len(msgs)):
    r = 0
    for bit in msgs[i]:
        r = (r >> 1) | (bit << 6)
    rs.append(r)
print(rs)

nexts = []
for i, r in enumerate(rs):
    r0 = (r >> 1)
    r1 = (r >> 1) | (1 << 6)
    nexts.append(((par[r0 & g1], par[r0 & g3]), (par[r1 & g1], par[r1 & g3])))


x = 0

for offset1 in range(0, 1):
    for offset2 in range(-32, 32):
        for i in range(len(msgs)):
            target = (frames[i + 32 + offset1][x], frames[i + 32 + offset2][x+1])
            if target not in nexts[i]:
                break
        else:
            good_offset1 = offset1
            good_offset2 = offset2
            print(good_offset1, good_offset2)

for i in range(len(msgs)):
    target = (frames[i + 32 + good_offset1][x], frames[i + 32 + good_offset2][x+1])
    bit = nexts[i].index(target)
    msgs[i].append(bit)
    rs[i] = (rs[i] >> 1) | (bit << 6)
x += 2

#######

for _ in range(10):
    nexts = []
    for i, r in enumerate(rs):
        r0 = (r >> 1)
        r1 = (r >> 1) | (1 << 6)
        nexts.append(((par[r0 & g1], par[r0 & g3]), (par[r1 & g1], par[r1 & g3])))

    for offset1 in range(-32, 32):
        for offset2 in range(-32, 32):
            for i in range(len(msgs)):
                target = (frames[i + 32 + offset1][x], frames[i + 32 + offset2][x+1])
                if target not in nexts[i]:
                    break
            else:
                good_offset1 = offset1
                good_offset2 = offset2
                print(good_offset1, good_offset2)

    for i in range(len(msgs)):
        target = (frames[i + 32 + good_offset1][x], frames[i + 32 + good_offset2][x+1])
        bit = nexts[i].index(target)
        msgs[i].append(bit)
        rs[i] = (rs[i] >> 1) | (bit << 6)
    x += 2

for msg in msgs:
    print(msg)
