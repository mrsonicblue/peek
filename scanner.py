import json
import pathlib
import sources
import subprocess
import log
from core import Core
from rom import Rom

class Scanner:
    def __init__(self, config):
        general = config['general']
        self.games_path = pathlib.Path(general['games_path'])
        self.meta_path = pathlib.Path(general['meta_path'])
        self.tabs_path = pathlib.Path(general['tabs_path'])
        self.peek_path = pathlib.Path(general['peek_path'])
        self.tab_headers = general['tab_headers']
        self.core_white_list = general['core_white_list']
        self.core_black_list = general['core_black_list']
        self.rom_max_size = general['rom_max_size']

        self.sources = self._sources(config)
        if len(self.sources) == 0:
            raise Exception("No configured sources")

    def _sources(self, config):
        result = []

        for source_key, source_cls in sources.all.items():
            if source_key not in config:
                raise Exception("No configuration section for source: " + source_key)

            source_config = config[source_key]
            if source_config['enabled']:
                result.append((source_key, source_config['priority'], source_cls(source_config)))
        
        result.sort(key=lambda s: s[1])

        return result

    def run(self):
        try:
            for source_key, _, source in self.sources:
                log.info("Opening source: {}", source_key)
                source.open()

            log.info('Scanning cores')

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
                log.core(core_index, core_count, core.name)

                self.core(core)            
        finally:
            for source_key, _, source in self.sources:
                log.info("Closing source: {}", source_key)
                try:
                    source.close()
                except:
                    pass
        
            log.status_erase()

    def just_files(self, path):
        result = path.iterdir()
        result = filter(lambda p: p.is_file(), result)
        result = filter(lambda p: not p.name.startswith('.'), result)
        return result
    
    def core(self, core):
        tab_path = self.tabs_path / (core.name + '.txt')
        self.build_tab(core, tab_path)
        self.import_tab(core, tab_path)

    def build_tab(self, core, tab_path):
        with tab_path.open('w', encoding='utf-8') as tab:
            tab.write('ROM')
            for header in self.tab_headers:
                tab.write('\t')
                tab.write(header)
            tab.write('\n')

            meta_files = {}
            for source_key, _, source in self.sources:
                meta_path = self.meta_path / source_key / core.name
                meta_path.mkdir(parents=True, exist_ok=True)
                meta_files[source_key] = { file:True for file in self.just_files(meta_path) }

            rom_paths = self.just_files(core.path)
            roms = map(lambda p: Rom(p, core), rom_paths)

            if self.rom_max_size > 0:
                roms = filter(lambda r: r.stat.st_size <= self.rom_max_size, roms)

            roms = list(roms)
            roms.sort(key=lambda r: r.name)

            rom_index = 0
            rom_count = len(roms)
            for rom in roms:
                rom_index += 1
                log.rom(rom_index, rom_count, rom.name)

                self.rom(rom, tab, meta_files)

            for source_key, _, source in self.sources:
                for meta_file in meta_files[source_key].keys():
                    meta_file.unlink()
    
    def rom(self, rom, tab, meta_files):
        merged = {}

        for source_key, _, source in self.sources:
            meta_path = self.meta_path / source_key / rom.core.name / (rom.name + ".json")
            if meta_files[source_key].pop(meta_path, False):
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
                data = None
                try:
                    data = source.rom_data(rom)
                except Exception as e:
                    log.warn('Error getting data for {} in source {}', rom.name, source_key)
                    log.warn(e)
                if data is not None:
                    meta['size'] = rom.stat.st_size
                    meta['mtime'] = rom.stat.st_mtime
                    meta['data'] = data

                    with meta_path.open('w', encoding='utf-8') as f:
                        json.dump(meta, f)
            
            data = meta.get('data', None)
            if data is not None:
                for k, v in data.items():
                    if v is not None:
                        merged[k] = v

        tab.write(rom.name)
        for header in self.tab_headers:
            tab.write('\t')

            value = merged.get(header, None)
            if value is not None:
                if isinstance(value, list):
                    tab.write("|".join(map(lambda s: self.clean(s), value)))
                else:
                    tab.write(self.clean(str(value)))

        tab.write('\n')

    def import_tab(self, core, tab_path):
        if not self.peek_path:
            return

        cmd = [str(self.peek_path), "db", "import", core.name, str(tab_path)]
        result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

        if result.returncode != 0:
            raise Exception("Import failed with code " + str(result.returncode))

    def clean(self, s):
        if s is None:
            return ""
        
        return s.strip().replace(",", "")