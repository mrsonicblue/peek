import pathlib

class Scanner:
    def __init__(self, config):
        general = config['general']
        self.games_path = pathlib.Path(general['games_path'])
        self.tabs_path = pathlib.Path(general['tabs_path'])
        self.peek_path = pathlib.Path(general['peek_path'])
        self.core_white_list = general['core_white_list']
        self.core_black_list = general['core_black_list']
        self.rom_max_size = general['rom_max_size']
    
    def run(self):
        core_paths = self.games_path.iterdir()
        core_paths = filter(lambda p: p.is_dir(), core_paths)
        
        if len(self.core_white_list) > 0:
            core_paths = filter(lambda p: p.name in self.core_white_list, core_paths)

        if len(self.core_black_list) > 0:
            core_paths = filter(lambda p: p.name not in self.core_black_list, core_paths)

        cores = []
        for core_path in core_paths:
            core = {
                'name': core_path.name,
                'path': core_path
            }

            cores.append(core)
        
        core_index = 0
        core_count = len(cores)
        for core in cores:
            core_index += 1
            print('{} of {}: {}'.format(core_index, core_count, core['name']))
            self.core(core)

    def core(self, core):
        rom_paths = core['path'].iterdir()
        rom_paths = filter(lambda p: p.is_file(), rom_paths)

        roms = map(lambda p: {
            'name': p.name,
            'path': p,
            'stat': p.stat()
        }, rom_paths)

        if self.rom_max_size > 0:
            roms = filter(lambda r: r['stat'].st_size, roms)
        
        for rom in roms:
            print('-- {}'.format(rom['name']))