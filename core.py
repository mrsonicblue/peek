class Core:
    def __init__(self, path):
        self.name = path.name
        self.path = path
        self.rom_header_size = self._rom_header_size()
    
    def _rom_header_size(self):
        if self.name == 'NES':
            return 16

        return 0