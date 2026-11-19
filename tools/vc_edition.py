#!/usr/bin/env python3
"""Build and pin the Super Mario Advance 4 "VC Edition" ROM image.

The VC Edition patch (SMA4_USA_Australia_Rev_1_to_USA_VC_Edition.xdelta) is an
RFC 3284 VCDIFF file. This tool applies it with no third-party dependency, so
regen.ps1 does not need xdelta3 on PATH.

  apply  Verify the stock AX4E rev 1 ROM, apply the patch, verify the Adler-32
         the patch carries, and write the patched image. When game_vc.toml is
         already pinned, the output must also match its SHA-1.
  pin    Apply as above, then record the patched SHA-1/MD5 in game_vc.toml and
         add the matching [[target]] to every preloaded mod manifest. Run it
         once; afterwards the identity is fixed like the stock one.

Neither the ROM nor the patch is copied into the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STOCK_SHA1 = "532f3307021637474b6dd37da059ca360f612337"
STOCK_SIZE = 0x00400000
VC_SIZE = 0x00800000
VC_TOML = ROOT / "game_vc.toml"
MANIFESTS = ROOT / "mods" / "preloaded" / "packages"
MOD_GAME_ID = "super-mario-advance-4-us-rev1"
UNPINNED = "UNPINNED"

ADD, RUN, COPY = 1, 2, 3


def _code_table() -> list[tuple[tuple[int, int, int], ...]]:
    """RFC 3284 section 5.6 default code table: (type, size, mode) pairs."""
    t: list[tuple[tuple[int, int, int], ...]] = [((RUN, 0, 0),)]
    t += [((ADD, s, 0),) for s in range(18)]
    for m in range(9):
        t.append(((COPY, 0, m),))
        t += [((COPY, s, m),) for s in range(4, 19)]
    for m in range(6):
        t += [((ADD, a, 0), (COPY, c, m)) for a in range(1, 5) for c in range(4, 7)]
    for m in range(6, 9):
        t += [((ADD, a, 0), (COPY, 4, m)) for a in range(1, 5)]
    t += [((COPY, 4, m), (ADD, 1, 0)) for m in range(9)]
    assert len(t) == 256
    return t


TABLE = _code_table()


class PatchError(ValueError):
    pass


class _Cursor:
    def __init__(self, data: bytes, pos: int, end: int):
        self.data, self.pos, self.end = data, pos, end

    def byte(self) -> int:
        if self.pos >= self.end:
            raise PatchError("truncated VCDIFF section")
        v = self.data[self.pos]
        self.pos += 1
        return v

    def int(self) -> int:
        v = 0
        for _ in range(10):
            b = self.byte()
            v = (v << 7) | (b & 0x7F)
            if v > 0xFFFFFFFF:
                raise PatchError("VCDIFF integer overflow")
            if not b & 0x80:
                return v
        raise PatchError("overlong VCDIFF integer")

    def take(self, n: int) -> bytes:
        if n > self.end - self.pos:
            raise PatchError("truncated VCDIFF section")
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v


def vcdiff_apply(source: bytes, patch: bytes) -> bytes:
    """Decode an RFC 3284 delta (default code table, no secondary compressor)."""
    if patch[:4] != b"\xd6\xc3\xc4\x00":
        raise PatchError("not a VCDIFF/xdelta patch")
    h = _Cursor(patch, 4, len(patch))
    ind = h.byte()
    if ind & 0x03:
        raise PatchError("secondary compression / custom code tables are unsupported")
    if ind & 0x04:
        h.take(h.int())
    out = bytearray()
    windows = 0
    while h.pos < h.end:
        wi = h.byte()
        if wi & 0x03 == 0x03:
            raise PatchError("invalid window indicator")
        seg_len = seg_pos = 0
        if wi & 0x03:
            seg_len, seg_pos = h.int(), h.int()
            limit = len(source) if wi & 0x01 else len(out)
            if seg_pos + seg_len > limit:
                raise PatchError("window segment lies outside the source")
        seg = (source if wi & 0x01 else bytes(out))[seg_pos:seg_pos + seg_len] if wi & 0x03 else b""
        delta_len = h.int()
        delta_end = h.pos + delta_len
        target_len = h.int()
        if h.byte() != 0:
            raise PatchError("per-section secondary compression is unsupported")
        data_len, inst_len, addr_len = h.int(), h.int(), h.int()
        adler = int.from_bytes(h.take(4), "big") if wi & 0x04 else None
        if data_len + inst_len + addr_len != delta_end - h.pos:
            raise PatchError("inconsistent window lengths")
        data = _Cursor(patch, h.pos, h.pos + data_len)
        inst = _Cursor(patch, data.end, data.end + inst_len)
        addr = _Cursor(patch, inst.end, delta_end)
        h.pos = delta_end

        win = bytearray()
        near = [0, 0, 0, 0]
        same = [0] * 768
        next_near = 0
        while inst.pos < inst.end:
            for kind, size, mode in TABLE[inst.byte()]:
                if size == 0:
                    size = inst.int()
                if size > target_len - len(win):
                    raise PatchError("instruction overruns its window")
                if kind == ADD:
                    win += data.take(size)
                elif kind == RUN:
                    win += bytes([data.byte()]) * size
                else:
                    here = seg_len + len(win)
                    if mode == 0:
                        a = addr.int()
                    elif mode == 1:
                        a = here - addr.int()
                    elif mode < 6:
                        a = near[mode - 2] + addr.int()
                    else:
                        a = same[(mode - 6) * 256 + addr.byte()]
                    if not 0 <= a < here:
                        raise PatchError("COPY address out of range")
                    near[next_near] = a
                    next_near = (next_near + 1) & 3
                    same[a % 768] = a
                    for k in range(size):  # byte-wise: overlapping copies replicate
                        at = a + k
                        win.append(seg[at] if at < seg_len else win[at - seg_len])
        if len(win) != target_len or data.pos != data.end or addr.pos != addr.end:
            raise PatchError("window did not decode to its declared length")
        if adler is not None and zlib.adler32(bytes(win)) != adler:
            raise PatchError("Adler-32 mismatch: wrong source ROM or corrupt patch")
        out += win
        windows += 1
    if not windows:
        raise PatchError("patch contains no windows")
    return bytes(out)


def pinned_sha1() -> str:
    m = re.search(r'(?ms)^\[identity\]\s*\n\s*sha1\s*=\s*"([^"]*)"', VC_TOML.read_text())
    if not m:
        raise SystemExit(f"{VC_TOML}: [identity].sha1 not found")
    return m.group(1).lower()


def build(rom: Path, patch: Path) -> bytes:
    stock = rom.read_bytes()
    got = hashlib.sha1(stock).hexdigest()
    if len(stock) != STOCK_SIZE or got != STOCK_SHA1:
        raise SystemExit(
            f"{rom}: not the stock AX4E rev 1 ROM (SHA-1 {got}, expected {STOCK_SHA1})")
    try:
        image = vcdiff_apply(stock, patch.read_bytes())
    except PatchError as e:
        raise SystemExit(f"{patch}: {e}")
    if len(image) != VC_SIZE or image[0xAC:0xB0] != b"AX4E":
        raise SystemExit(f"{patch}: output is not an 8 MiB AX4E image")
    return image


def cmd_apply(args: argparse.Namespace) -> int:
    image = build(args.rom, args.patch)
    sha1 = hashlib.sha1(image).hexdigest()
    pinned = pinned_sha1()
    if pinned != UNPINNED.lower() and sha1 != pinned:
        raise SystemExit(
            f"patched SHA-1 {sha1} differs from game_vc.toml ({pinned}); "
            "this is a different patch release")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    tmp = args.out.with_suffix(args.out.suffix + ".tmp")
    tmp.write_bytes(image)
    tmp.replace(args.out)
    print(f"wrote {args.out}\n  sha1 {sha1}\n  md5  {hashlib.md5(image).hexdigest()}")
    if pinned == UNPINNED.lower():
        print("game_vc.toml is unpinned; run `tools/vc_edition.py pin` to record this identity.")
    return 0


def cmd_pin(args: argparse.Namespace) -> int:
    image = build(args.rom, args.patch)
    sha1 = hashlib.sha1(image).hexdigest()
    md5 = hashlib.md5(image).hexdigest()
    text = VC_TOML.read_text()
    current = pinned_sha1()
    if current not in (UNPINNED.lower(), sha1):
        raise SystemExit(f"game_vc.toml is already pinned to {current}; refusing to change it")
    text = re.sub(r'(?m)^(sha1\s*=\s*)"UNPINNED"', rf'\g<1>"{sha1}"', text)
    text = re.sub(r'(?m)^(md5\s*=\s*)"UNPINNED"', rf'\g<1>"{md5}"', text)
    VC_TOML.write_text(text)
    target = f'[[target]]\ngame_id = "{MOD_GAME_ID}"\nrom_sha1 = "{sha1}"\n'
    for manifest in sorted(MANIFESTS.glob("*/*/manifest.toml")):
        body = manifest.read_text()
        if f'rom_sha1 = "{sha1}"' in body:
            continue
        stock = f'[[target]]\ngame_id = "{MOD_GAME_ID}"\nrom_sha1 = "{STOCK_SHA1}"\n'
        if stock not in body:
            raise SystemExit(f"{manifest}: stock [[target]] block not found")
        manifest.write_text(body.replace(stock, stock + "\n" + target, 1))
        print(f"added VC target to {manifest.relative_to(ROOT)}")
    print(f"pinned game_vc.toml\n  sha1 {sha1}\n  md5  {md5}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("apply", "pin"):
        p = sub.add_parser(name)
        p.add_argument("--rom", type=Path, required=True, help="stock AX4E rev 1 ROM")
        p.add_argument("--patch", type=Path, required=True, help="VC Edition .xdelta")
        if name == "apply":
            p.add_argument("--out", type=Path,
                           default=ROOT / "roms" / "super_mario_advance_4_usa_vc.gba")
    args = ap.parse_args()
    return cmd_apply(args) if args.cmd == "apply" else cmd_pin(args)


if __name__ == "__main__":
    sys.exit(main())
