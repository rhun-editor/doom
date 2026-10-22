#!/usr/bin/env python3
"""Exercise the PNG frame transport independently of the editor and engine."""
import ctypes
from pathlib import Path
import struct
import subprocess
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]

class PNG(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(['clang','-shared','-fPIC','-Isrc','src/png.c','-lz','-o','build/png-test.dylib'],cwd=ROOT,check=True)
        cls.lib=ctypes.CDLL(str(ROOT/'build/png-test.dylib'))
        cls.lib.doom_png.restype=ctypes.c_size_t
        cls.pixels=(ctypes.c_ubyte*64000)(*(i%256 for i in range(64000)))
        cls.palette=(ctypes.c_ubyte*768)(*(i%256 for i in range(768)))

    def test_frame_is_a_valid_lossless_indexed_png(self):
        out=(ctypes.c_ubyte*67000)()
        n=self.lib.doom_png(self.pixels,self.palette,out,len(out))
        self.assertGreater(n,800)
        data=bytes(out[:n]);self.assertEqual(data[:8],b'\x89PNG\r\n\x1a\n')
        at=8;chunks={}
        while at<len(data):
            length=struct.unpack('>I',data[at:at+4])[0]
            name=data[at+4:at+8];body=data[at+8:at+8+length]
            self.assertEqual(struct.unpack('>I',data[at+8+length:at+12+length])[0],zlib.crc32(name+body))
            chunks[name]=body;at+=length+12
        self.assertEqual(struct.unpack('>IIBBBBB',chunks[b'IHDR']),(320,200,8,3,0,0,0))
        self.assertEqual(chunks[b'PLTE'],bytes(self.palette))
        raw=zlib.decompress(chunks[b'IDAT'])
        self.assertEqual(len(raw),200*321)
        for y in range(200):
            self.assertEqual(raw[y*321],0)
            self.assertEqual(raw[y*321+1:y*321+321],bytes(self.pixels[y*320:(y+1)*320]))

    def test_short_output_buffer_is_not_written(self):
        out=(ctypes.c_ubyte*64)(*([123]*64))
        self.assertEqual(self.lib.doom_png(self.pixels,self.palette,out,64),0)
        self.assertEqual(bytes(out),bytes([123]*64))

if __name__=='__main__': unittest.main()
