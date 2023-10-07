#!/usr/bin/env python3

import datetime
import struct

def decode_coords(data):
    # num = int(data.hex(), 16)
    # lat1 = ((num >> 78) & 0x1ffffff) / 100000
    # if ((num >> 103) & 1):
    #     lat1 = -lat1
    # lon1 = ((num >> 52) & 0x1ffffff) / 100000
    # if ((num >> 77) & 1):
    #     lon1 = -lon1
    # lat2 = ((num >> 26) & 0x1ffffff) / 100000
    # if ((num >> 51) & 1):
    #     lat2 = -lat2
    # lon2 = ((num >> 0) & 0x1ffffff) / 100000
    # if ((num >> 25) & 1):
    #     lon2 = -lon2
    num = int(data.hex(), 16)
    lat1 = ((num >> 79) & 0x1ffffff) / 100000
    if ((num >> 78) & 1):
        lat1 = -lat1
    lon1 = ((num >> 53) & 0x1ffffff) / 100000
    if ((num >> 53) & 1):
        lon1 = -lon1
    lat2 = ((num >> 27) & 0x1ffffff) / 100000
    if ((num >> 27) & 1):
        lat2 = -lat2
    lon2 = ((num >> 0) & 0x1ffffff) / 100000
    if ((num >> 1) & 1):
        lon2 = -lon2
    print()
    print(f"{lat1:.5f},{lon1:.5f},red,marker")
    print(f"{lat2:.5f},{lon2:.5f},red,marker")



ii = 0
for filename in ("image1.dat", "image2.dat", "dc-1035.dat", "detr-1027.dat", "miss-950-res.dat", "san-949.dat", "vegas-889.dat"):
    print(filename)
    with open(filename, "rb") as f:
        data = f.read()

        header = bytes([0xff, 0xf7, 0xff, 0xf7])
        while True:
            index = data.find(header)
            if index == -1:
                break
            data = data[index:]
            if len(data) < 6:
                break

            offset = 4

            payload_length = struct.unpack(">H", data[offset:offset + 2])[0]
            offset += 2
            if len(data) < 8 + payload_length:
                break
            
            print(data[offset:offset+27].hex(), end=" ")
            print(datetime.datetime.utcfromtimestamp(int(data[offset+9:offset+13].hex(), 16)).strftime('%Y-%m-%d %H:%M:%S'))
            decode_coords(data[offset+14:offset+27])
            offset += 27

            filename_len = data[offset]
            offset += 1

            filename = data[offset:offset + filename_len].decode()
            offset += filename_len

            print(filename, end=" ")

            print(data[offset:offset+4].hex(), end=" ")
            offset += 4

            file_length = struct.unpack(">H", data[offset:offset + 2])[0]
            offset += 2

            ii += 1
            with open(f"{ii:03}-{filename}", "wb") as f2:
                f2.write(data[offset:offset + file_length])
            
            print(data[offset + file_length:offset + file_length + 2].hex())
            
            data = data[8 + payload_length:]
