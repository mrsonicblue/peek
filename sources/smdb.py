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
        self.map_rules = None

    def open(self):
        self.ensure_database()

    def close(self):
        pass

    def ensure_database(self):
        if self.database_path.is_dir():
            return

        log.crit('Downloading Hardware Target Game Database')
        try:
            self.database_path.mkdir(parents=True, exist_ok=True)

            url = "https://github.com/frederic-mahe/Hardware-Target-Game-Database/archive/refs/heads/master.zip"
            with requests.get(url) as r:
                r.raise_for_status()
                with io.BytesIO() as mem:
                    mem.write(r.content)

                    log.crit('Extracting Hardware Target Game Database')

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
        self.map_rules = None

        full = lambda bit: bit['full']
        name_equals = lambda value: lambda bit: SmdbSource.bit_equals(bit, 'name', value)
        name_search = lambda value: lambda bit: SmdbSource.bit_search(bit, 'name', value)
        name_extract = lambda value: lambda bit: SmdbSource.bit_extract(bit, 'name', value)

        if core.name == 'NES':
            self.map = self.read_map('EverDrive N8 & PowerPak SMDB.txt')
        elif core.name == 'SNES':
            self.map = self.read_map('Super EverDrive & SD2SNES SMDB.txt')
        elif core.name == 'GBA':
            self.map = self.read_map('EverDrive GBA SMDB.txt')
        elif core.name == 'GAMEBOY':
            self.map = self.read_map('EverDrive GB SMDB.txt')
        elif core.name == 'Genesis':
            self.map = self.read_map('Mega EverDrive SMDB.txt')
        elif core.name == 'ATARI2600':
            self.map = self.read_map('Atari 2600 SMDB.txt')
        
        self.map_rules = [
            ('Year',      [None, name_equals('Game Series Collections'), name_equals('Chronological'), name_extract(r' ([0-9]{4})$')]),
            ('Developer', [None, name_equals('Game Series Collections'), name_equals('Developer'), name_extract(r'^Developer (.*)$')]),
            ('Publisher', [None, name_equals('Game Series Collections'), name_equals('Developer'), name_extract(r'^Publisher (.*)$')]),
            ('Genre',     [None, name_equals('Game Series Collections'), name_equals('Genre'), full]),
            ('Franchise', [None, name_equals('Game Series Collections'), name_equals('Franchise'), full])
        ]

        if core.name == 'GBA':
            self.map_rules.append(('List', [None, name_equals('Game Series Collections'), name_equals('Best-of Lists'), full]))
        elif core.name == 'GAMEBOY':
            self.map_rules.append(('List', [None, name_equals('Game Series Collections'), name_equals('Best-of Lists'), None, full]))
        else:
            self.map_rules.append(('List', [None, name_equals('Game Series Collections'), name_equals('Best-Of Lists'), full]))

        self.map_rules.append(('Region', [None, name_extract(r'^(.*) - [0-9A-Z]-?[0-9A-Z]?$')]))

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
            #log.debug('{}-------------------------', rom.name)
            self.run_map_rules(rom, paths, result)
            #log.debug(result)

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
        if len(first_word) == 2 and first_word[0].isnumeric():
            result['index'] = int(first_word[0])
            bit = first_word[1]
        else:
            result['index'] = None

        result['name'] = bit

        return result

    @staticmethod
    def bit_equals(bit, key, value):
        return bit[key] == value

    @staticmethod
    def bit_search(bit, key, expr):
        m = re.search(expr, bit[key])
        return m is not None

    @staticmethod
    def bit_extract(bit, key, expr):
        m = re.search(expr, bit[key])
        if m is None:
            return None
        return m.group(1)

    def run_map_rules(self, rom, paths, result):
        for path in paths:
            used = False
            for result_key, rule in self.map_rules:
                if len(path) < len(rule):
                    continue

                value = None
                for path_bit, rule_bit in zip(path, rule):
                    if rule_bit is None:
                        value = True
                    else:
                        value = rule_bit(path_bit)
                    if not value:
                        break

                if value:
                    value = str(value)
                    values = result.setdefault(result_key, [])
                    if value not in values:
                        values.append(value)
                    used = True
                    break

            # if not used:
            #     log.debug('FELL THRU: {}', "/".join(map(lambda p: p['full'], path)))