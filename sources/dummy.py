import pathlib
import sqlite3

class DummySource:
    def __init__(self, config):
        pass

    def open(self):
        pass

    def close(self):
        pass

    def core_start(self, core):
        pass

    def core_end(self):
        pass

    def rom_data(self, rom):
        return {
            'Dummy': rom.name[0],
        }