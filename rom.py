import hashlib
import zipfile
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

    def read_chunks(self, f):
        # Skip header, if necessary
        if self.core.rom_header_size > 0:
            f.read(self.core.rom_header_size)

        while True:
            chunk = f.read(Rom.BUF_SIZE)
            if not chunk:
                break

            yield chunk

    def open_file(self):
        if self.path.suffix == '.zip':
            with zipfile.ZipFile(str(self.path)) as zip:
                zipfiles = zip.namelist()
                if len(zipfiles) == 0:
                    raise Exception('No files in archive')

                with zip.open(zipfiles[0]) as f:
                    for chunk in self.read_chunks(f):
                        yield chunk
        else:
            with open(str(self.path), 'rb') as f:
                for chunk in self.read_chunks(f):
                    yield chunk

    def hash(self):
        crc32 = 0
        md5 = hashlib.md5()
        sha1 = hashlib.sha1()
        for chunk in self.open_file():
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