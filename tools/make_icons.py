#!/usr/bin/env python3
"""Genera le icone PNG (192 e 512 px) della web-app e le incorpora in
firmware/LiFePO4_Tester/web_icons.h come array PROGMEM.
Nessuna dipendenza esterna (solo zlib della libreria standard)."""
import struct, zlib, os

def png(size, pixels):
    """pixels: funzione (x,y) -> (r,g,b,a)"""
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filtro "none"
        for x in range(size):
            raw.extend(pixels(x, y))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += chunk(b"IEND", b"")
    return out

def icon(size):
    s = size
    r_corner = s * 0.18
    bg = (16, 24, 40)          # sfondo blu scuro
    green = (52, 211, 153)     # verde LiFePO4
    white = (240, 245, 250)
    # geometria batteria (orizzontale)
    bx0, bx1 = s * 0.14, s * 0.78
    by0, by1 = s * 0.30, s * 0.70
    wall = s * 0.045
    tip_x0, tip_x1 = bx1, s * 0.86
    tip_y0, tip_y1 = s * 0.42, s * 0.58
    fill_x1 = bx0 + wall + (bx1 - bx0 - 2 * wall) * 0.72
    def inside_round(x, y):
        # quadrato con angoli arrotondati
        cx = min(max(x, r_corner), s - r_corner)
        cy = min(max(y, r_corner), s - r_corner)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r_corner ** 2
    def px(x, y):
        fx, fy = x + 0.5, y + 0.5
        if not inside_round(fx, fy):
            return (0, 0, 0, 0)
        col = bg
        if bx0 <= fx <= bx1 and by0 <= fy <= by1:
            if (fx < bx0 + wall or fx > bx1 - wall or fy < by0 + wall or fy > by1 - wall):
                col = white
            elif bx0 + wall * 1.6 <= fx <= fill_x1 and by0 + wall * 1.6 <= fy <= by1 - wall * 1.6:
                col = green
        if tip_x0 <= fx <= tip_x1 and tip_y0 <= fy <= tip_y1:
            col = white
        # fulmine stilizzato al centro
        lx, ly = (fx - s * 0.46) / s, (fy - s * 0.50) / s
        if abs(lx) < 0.035 and abs(ly) < 0.13:
            if (ly < 0 and lx > -0.035 + (-ly) * 0.25 - 0.02) or (ly >= 0 and lx < 0.035 - ly * 0.25 + 0.02):
                col = bg
        return (col[0], col[1], col[2], 255)
    return png(s, px)

def to_c_array(name, data):
    lines = [f"const uint8_t {name}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    fw = os.path.join(here, "..", "firmware", "LiFePO4_Tester")
    i192, i512 = icon(192), icon(512)
    with open(os.path.join(here, "icon-192.png"), "wb") as f: f.write(i192)
    with open(os.path.join(here, "icon-512.png"), "wb") as f: f.write(i512)
    hdr = "#pragma once\n// Generato da tools/make_icons.py - NON modificare a mano\n#include <pgmspace.h>\n\n"
    hdr += to_c_array("ICON_192_PNG", i192) + "\n\n" + to_c_array("ICON_512_PNG", i512) + "\n"
    with open(os.path.join(fw, "web_icons.h"), "w") as f: f.write(hdr)
    print(f"icon-192.png {len(i192)} B, icon-512.png {len(i512)} B -> web_icons.h")
