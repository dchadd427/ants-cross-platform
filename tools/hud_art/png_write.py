"""Writes the sheets: a PNG of 8-bit RGBA, with its deflate stream compressed as small as libdeflate (https://github.com/ebiggers/libdeflate) can, about 5% under what zlib's best level gives.

Pillow writes the file (it chooses the filter of every row); when the libdeflate library is on the machine (`brew install libdeflate`, `apt install libdeflate0`), the stream of the file is then compressed
again at libdeflate's highest level. The pixels are the same either way and the game reads either file: a machine without the library writes a file about 5% larger, never a different picture.
"""
import ctypes
import ctypes.util
import io
import struct
import zlib

from PIL import Image

LEVEL = 12                      # libdeflate's highest


def _library():
    """the libdeflate library, or None"""
    for name in (ctypes.util.find_library('deflate'), '/opt/homebrew/lib/libdeflate.dylib', '/usr/local/lib/libdeflate.dylib', 'libdeflate.so.0', 'libdeflate.so'):
        if not name:
            continue
        try:
            lib = ctypes.CDLL(name)
            lib.libdeflate_alloc_compressor.restype = ctypes.c_void_p
            lib.libdeflate_alloc_compressor.argtypes = [ctypes.c_int]
            lib.libdeflate_free_compressor.argtypes = [ctypes.c_void_p]
            lib.libdeflate_zlib_compress_bound.restype = ctypes.c_size_t
            lib.libdeflate_zlib_compress_bound.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
            lib.libdeflate_zlib_compress.restype = ctypes.c_size_t
            lib.libdeflate_zlib_compress.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
            return lib
        except (OSError, AttributeError):
            continue
    return None


def _chunks(data):
    at = 8
    while at + 12 <= len(data):
        n = struct.unpack('>I', data[at:at + 4])[0]
        yield data[at + 4:at + 8], data[at + 8:at + 8 + n]
        at += 12 + n


def _chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)


def png_bytes(rgba):
    """the PNG file (bytes) of an RGBA numpy array"""
    buf = io.BytesIO()
    Image.fromarray(rgba, 'RGBA').save(buf, 'PNG', optimize=True, compress_level=9)
    data = buf.getvalue()
    lib = _library()
    if lib is None:
        return data
    parts = dict(_chunks(data))
    raw = zlib.decompress(b''.join(body for kind, body in _chunks(data) if kind == b'IDAT'))
    comp = lib.libdeflate_alloc_compressor(LEVEL)
    try:
        cap = lib.libdeflate_zlib_compress_bound(comp, len(raw))
        out = ctypes.create_string_buffer(cap)
        n = lib.libdeflate_zlib_compress(comp, raw, len(raw), out, cap)
    finally:
        lib.libdeflate_free_compressor(comp)
    if n == 0 or n >= len(data):
        return data
    packed = b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', parts[b'IHDR']) + _chunk(b'IDAT', out.raw[:n]) + _chunk(b'IEND', b'')
    assert zlib.decompress(out.raw[:n]) == raw
    return packed


def write_png(path, rgba):
    with open(path, 'wb') as f:
        f.write(png_bytes(rgba))
