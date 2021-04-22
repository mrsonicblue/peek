import json
import pathlib
import sources
from core import Core
from rom import Rom

class Scanner:
    def __init__(self, config):
        general = config['general']
        self.games_path = pathlib.Path(general['games_path'])
        self.meta_path = pathlib.Path(general['meta_path'])
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

        for key, source_cls in sources.all.items():
            if key not in config:
                raise Exception("No configuration section for source: " + key)

            source_config = config[key]
            if source_config['enabled']:
                result.append((key, source_config['priority'], source_cls(source_config)))
        
        result.sort(key=lambda s: s[1])

        return result

    def run(self):
        try:
            for _, _, source in self.sources:
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
            for _, _, source in self.sources:
                try:
                    source.close()
                except:
                    pass

    def just_files(self, path):
        result = path.iterdir()
        result = filter(lambda p: p.is_file(), result)
        result = filter(lambda p: not p.name.startswith('.'), result)
        return result
    
    def core(self, core):
        meta_files = {}
        for key, _, source in self.sources:
            meta_path = self.meta_path / key / core.name
            meta_path.mkdir(parents=True, exist_ok=True)
            meta_files[key] = { file:True for file in self.just_files(meta_path) }

        rom_paths = self.just_files(core.path)

        roms = map(lambda p: Rom(p, core), rom_paths)

        if self.rom_max_size > 0:
            roms = filter(lambda r: r.stat.st_size <= self.rom_max_size, roms)
        
        for rom in roms:
            print('-- {}'.format(rom.name))

            self.rom(rom, meta_files)

        for key, _, source in self.sources:
            for meta_file in meta_files[key].keys():
                meta_file.unlink()
    
    def rom(self, rom, meta_files):
        for key, _, source in self.sources:
            meta_path = self.meta_path / key / rom.core.name / (rom.name + ".json")
            if meta_files[key].pop(meta_path, False):
                try:
                    with meta_path.open('r', encoding='utf-8') as f:
                        meta = json.load(f)
                except:
                    meta = {}
            else:
                meta = {}

            size = meta.get('size', -1)
            mtime = meta.get('mtime', -1)

            if rom.stat.st_size != size or rom.stat.st_mtime != mtime:
                print('--- Updating data for ' + key)

                meta['size'] = rom.stat.st_size
                meta['mtime'] = rom.stat.st_mtime
                meta['data'] = source.rom_data(rom)

                with meta_path.open('w', encoding='utf-8') as f:
                    json.dump(meta, f)