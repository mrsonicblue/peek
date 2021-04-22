import pathlib
import sqlite3

class DummySource:
    def __init__(self, config):
        pass

    def open(self):
        pass

    def rom_data(self, rom):
        return {
            'Dummy': rom.name[0],
        }

    def close(self):
        pass