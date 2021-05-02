import io
import log
import pathlib
import re
import requests
import shutil
import zipfile

class SmdbSource:
    def __init__(self, config):
        self.conn = None
        self.database_path = pathlib.Path(config['database_path'])
        self.map = None
        self.mapper = None

    def open(self):
        self.ensure_database()

    def close(self):
        pass

    def ensure_database(self):
        if self.database_path.is_dir():
            return

        log.info('Downloading Hardware Target Game Database')
        try:
            self.database_path.mkdir(parents=True, exist_ok=True)

            url = "https://github.com/frederic-mahe/Hardware-Target-Game-Database/archive/refs/heads/master.zip"
            with requests.get(url) as r:
                r.raise_for_status()
                with io.BytesIO() as mem:
                    mem.write(r.content)

                    log.info('Extracting Hardware Target Game Database')

                    with zipfile.ZipFile(mem) as zip:
                        for name in zip.namelist():
                            bits = name.split('/')
                            if len(bits) != 3:
                                continue
                            if bits[0] != 'Hardware-Target-Game-Database-master':
                                continue
                            if bits[1] != 'EverDrive Pack SMDBs':
                                continue
                            if not bits[2].endswith('.txt'):
                                continue

                            path = self.database_path / bits[2]
                            with zip.open(name) as zf, open(str(path), 'wb') as f:
                                shutil.copyfileobj(zf, f)
        except Exception as e:
            if self.database_path.is_dir():
                shutil.rmtree(str(self.database_path))
            raise e

    def core_start(self, core):
        self.map = None
        self.mapper = None

        if core.name == 'NES':
            self.map = self.read_map('EverDrive N8 & PowerPak SMDB.txt')
            self.mapper = self.nes_mapper

        if self.map is None:
            log.warn('No smdb map exists for core {}', core.name)

    def read_map(self, filename):
        map_path = self.database_path / filename
        if not map_path.is_file():
            raise Exception('Map file missing: {}'.format(str(map_path)))

        map = {}
        with map_path.open('r', encoding='utf-8') as f:
            for line in f:
                bits = line.split('\t')
                if len(bits) != 5:
                    continue

                sha256, path, sha1, md5, crc32 = bits
                map.setdefault(sha1, []).append(path)

        return map

    def core_end(self):
        pass

    def rom_data(self, rom):
        if self.map is None:
            return None

        paths = self.map.get(rom.sha1(True), None)
        
        result = {}
        if paths is not None:
            paths = SmdbSource.parse_paths(paths)
            log.debug('{}-------------------------', rom.name)
            self.mapper(rom, paths, result)
        else:
            log.info('Missing: {}', rom.name)

        return result

    @staticmethod
    def parse_paths(paths):
        return list(map(SmdbSource.parse_path, paths))

    @staticmethod
    def parse_path(path):
        return list(map(SmdbSource.parse_path_bit, path.split('/')))

    @staticmethod
    def parse_path_bit(bit):
        result = {
            'full': bit
        }

        first_word = bit.split(' ', 1)
        if first_word[0].isnumeric():
            result['index'] = int(first_word[0])
            bit = first_word[1]
        else:
            result['index'] = None

        result['name'] = bit

        return result

    def nes_mapper(self, rom, paths, result):
        regions = []
        genres = []
        year = None
        bestofs = []
        for path in paths:
            used = False

            if len(path) > 1:
                if path[1]['name'] == 'Game Series Collections':
                    if len(path) > 2:
                        if path[2]['name'] == 'Chronological':
                            if len(path) > 3:
                                m = re.search(r' ([0-9]{4})$', path[3]['name'])
                                if m:
                                    year = m.group(1)
                                    used = True
                        elif path[2]['name'] == 'Best-Of Lists':
                            if len(path) > 3:
                                bestofs.append(path[3]['full'])
                                used = True
                else:
                    m = re.match(r'^(.*) - [A-Z]-[A-Z]$', path[1]['name'])
                    if m:
                        regions.append(m.group(1))
                        used = True

            if not used:
                log.debug('FELL THRU: {}', "/".join(map(lambda p: p['full'], path)))

        if len(regions) > 0:
            result['Region'] = regions

        if len(genres) > 0:
            result['Genre'] = genres

        if year is not None:
            result['Year'] = year

        if len(bestofs) > 0:
            result['Best-Of'] = bestofs

        log.debug(result)