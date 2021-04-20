import sqlite3

class SqliteSource(config):
    def __init__(self, config):
        self.a = "a"
    
    def rom_data(self, rom_header):
        return {
            'Year': '1982',
            'Genre': ['Action', '2D']
        }