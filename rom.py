import hashlib
import zlib

class Rom:
    BUF_SIZE = 65536

    def __init__(self, path, core):
        self.name = path.name
        self.path = path
        self.stat = path.stat()
        self.core = core
        self._crc32 = None
        self._md5 = None
        self._sha1 = None

    def size(self):
        return self.stat.st_size

    def read_chunks(self):
        with open(str(self.path), 'rb') as f:
            while True:
                data = f.read(Rom.BUF_SIZE)
                if not data:
                    break

                yield data

    def hash(self, hash_cls):
        h = hash_cls()
        for chunk in self.read_chunks():
            h.update(chunk)

        return h.hexdigest()

    def crc32(self):
        if self._crc32 is None:
            v = 0
            for chunk in self.read_chunks():
                v = zlib.crc32(chunk, v)

            self._crc32 = format(v & 0xFFFFFFFF, '08x')

        return self._crc32

    def md5(self):
        if self._md5 is None:
            self._md5 = self.hash(hashlib.md5)

        return self._md5

    def sha1(self):
        if self._sha1 is None:
            self._sha1 = self.hash(hashlib.sha1)

        return self._sha1