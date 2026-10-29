"""Private ABC9 lab film packing; no device access or public format registration."""
from __future__ import annotations

import struct

from PIL import Image

import panel_lab as lab


def _validate(image: Image.Image, maximum: int, *, abc9: bool = False) -> None:
    if image.size != lab.PORTRAIT or image.mode not in (("I",) if abc9 else ("I", "P", "L")):
        raise ValueError("Expected 480x720 integer index canvas" + (" in PIL I mode" if abc9 else ""))
    low, high = image.getextrema()
    if low < 0 or high > maximum:
        raise ValueError(f"Expected physical indices 0..{maximum}")


def pack_abc9_film(portrait_indices: Image.Image) -> bytes:
    """Pack I-mode 0..511 indices into the private single-frame lab container."""
    _validate(portrait_indices, 511, abc9=True)
    native = lab.native_image(portrait_indices)
    body = struct.pack(f"<{lab.NATIVE[0] * lab.NATIVE[1]}H", *native.getdata())
    header = struct.pack("<IHHBBH4s16x", len(body), *lab.NATIVE, 0, 0x7F, 1, b"ABC9")
    return header + body


def pack_qual_film(portrait_indices_0_63: Image.Image) -> bytes:
    """Pack the existing Format2 indices (including any lab marker set by caller)."""
    _validate(portrait_indices_0_63, 63)
    indexed = Image.new("P", lab.PORTRAIT)
    indexed.putdata(list(portrait_indices_0_63.getdata()))
    return lab.film_bytes([indexed], 2)
