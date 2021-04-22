import hashlib
import zlib

class Rom:
    BUF_SIZE = 65536

    def __init__(self, path, core):
        self.name = path.name
        self.path = path
        self.stat = path.stat()
        self.core = core
        self._hashed = False
        self._crc32 = None
        self._md5 = None
        self._sha1 = None

    def read_chunks(self):
        with open(str(self.path), 'rb') as f:
            while True:
                data = f.read(Rom.BUF_SIZE)
                if not data:
                    break

                yield data

    def hash(self):
        crc32 = 0
        md5 = hashlib.md5()
        sha1 = hashlib.sha1()
        for chunk in self.read_chunks():
            crc32 = zlib.crc32(chunk, crc32)
            md5.update(chunk)
            sha1.update(chunk)

        self._hashed = True
        self._crc32 = format(crc32 & 0xFFFFFFFF, '08x')
        self._md5 = md5.hexdigest()
        self._sha1 = sha1.hexdigest()

    def crc32(self):
        if not self._hashed:
            self.hash()

        return self._crc32

    def md5(self):
        if not self._hashed:
            self.hash()

        return self._md5

    def sha1(self):
        if not self._hashed:
            self.hash()

        return self._sha1