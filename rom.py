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
        self._header_crc32 = None
        self._header_md5 = None
        self._header_sha1 = None
        self._noheader_crc32 = None
        self._noheader_md5 = None
        self._noheader_sha1 = None

    def read_chunks(self, f):
        # If header exists, first chunk needs to be that big
        if self.core.rom_header_size > 0:
            yield f.read(self.core.rom_header_size)

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
        # Some databases skip a portion of the file when hashing
        # for some systems. For those systems, we need to calculate
        # two sets of hashes.
        if self.core.rom_header_size > 0:
            self.hash_nonzero_header()
        else:
            self.hash_zero_header()

    def hash_nonzero_header(self):
        header_crc32 = 0
        header_md5 = hashlib.md5()
        header_sha1 = hashlib.sha1()
        noheader_crc32 = 0
        noheader_md5 = hashlib.md5()
        noheader_sha1 = hashlib.sha1()

        chunk_iter = iter(self.open_file())

        try:
            chunk = next(chunk_iter)
            header_crc32 = zlib.crc32(chunk, header_crc32)
            header_md5.update(chunk)
            header_sha1.update(chunk)
        except StopIteration:
            pass

        while True:
            try:
                chunk = next(chunk_iter)
                header_crc32 = zlib.crc32(chunk, header_crc32)
                header_md5.update(chunk)
                header_sha1.update(chunk)
                noheader_crc32 = zlib.crc32(chunk, noheader_crc32)
                noheader_md5.update(chunk)
                noheader_sha1.update(chunk)
            except StopIteration:
                break

        self._hashed = True
        self._header_crc32 = format(header_crc32 & 0xFFFFFFFF, '08x')
        self._header_md5 = header_md5.hexdigest()
        self._header_sha1 = header_sha1.hexdigest()
        self._noheader_crc32 = format(noheader_crc32 & 0xFFFFFFFF, '08x')
        self._noheader_md5 = noheader_md5.hexdigest()
        self._noheader_sha1 = noheader_sha1.hexdigest()

    def hash_zero_header(self):
        crc32 = 0
        md5 = hashlib.md5()
        sha1 = hashlib.sha1()
        for chunk in self.open_file():
            crc32 = zlib.crc32(chunk, crc32)
            md5.update(chunk)
            sha1.update(chunk)

        self._hashed = True
        self._header_crc32 = format(crc32 & 0xFFFFFFFF, '08x')
        self._header_md5 = md5.hexdigest()
        self._header_sha1 = sha1.hexdigest()
        self._noheader_crc32 = self._header_crc32
        self._noheader_md5 = self._header_md5
        self._noheader_sha1 = self._header_sha1

    def crc32(self, with_header=True):
        if not self._hashed:
            self.hash()

        if with_header:
            return self._header_crc32

        return self._noheader_crc32

    def md5(self, with_header=True):
        if not self._hashed:
            self.hash()

        if with_header:
            return self._header_md5

        return self._noheader_md5

    def sha1(self, with_header=True):
        if not self._hashed:
            self.hash()

        if with_header:
            return self._header_sha1

        return self._noheader_sha1