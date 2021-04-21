import pathlib
import sources
from core import Core
from rom import Rom

class Scanner:
    def __init__(self, config):
        general = config['general']
        self.games_path = pathlib.Path(general['games_path'])
        self.tabs_path = pathlib.Path(general['tabs_path'])
        self.peek_path = pathlib.Path(general['peek_path'])
        self.core_white_list = general['core_white_list']
        self.core_black_list = general['core_black_list']
        self.rom_max_size = general['rom_max_size']

        self.sources = self._sources(config)
        if len(self.sources) == 0:
            raise Exception("No configured sources")

    def _sources(self, config):
        result = []

        openvgdb = config['openvgdb']
        if openvgdb['enabled']:
            result.append(sources.OpenVgdbSource(openvgdb))

        return result

    def run(self):
        try:
            for source in self.sources:
                source.open()

            core_paths = self.games_path.iterdir()
            core_paths = filter(lambda p: p.is_dir(), core_paths)
            core_paths = filter(lambda p: not p.name.startswith('.'), core_paths)
            
            if len(self.core_white_list) > 0:
                core_paths = filter(lambda p: p.name in self.core_white_list, core_paths)

            if len(self.core_black_list) > 0:
                core_paths = filter(lambda p: p.name not in self.core_black_list, core_paths)

            cores = list(map(lambda p: Core(p), core_paths))
            cores.sort(key=lambda c: c.name)
            
            core_index = 0
            core_count = len(cores)
            for core in cores:
                core_index += 1
                print('{} of {}: {}'.format(core_index, core_count, core.name))

                self.core(core)
        finally:
            for source in self.sources:
                try:
                    source.close()
                except:
                    pass

    def core(self, core):
        rom_paths = core.path.iterdir()
        rom_paths = filter(lambda p: p.is_file(), rom_paths)
        rom_paths = filter(lambda p: not p.name.startswith('.'), rom_paths)

        roms = map(lambda p: Rom(p, core), rom_paths)

        if self.rom_max_size > 0:
            roms = filter(lambda r: r.size() <= self.rom_max_size, roms)
        
        for rom in roms:
            print('-- {}'.format(rom.name))
            print('---- CRC32: ' + rom.crc32())
            print('---- MD5: ' + rom.md5())
            print('---- SHA1: ' + rom.sha1())

            self.rom(rom)
    
    def rom(self, rom):
        for source in self.sources:
            hmm = source.rom_data(rom)
            print(str(hmm))