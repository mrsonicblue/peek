import pathlib
import sqlite3

class OpenVgdbSource:
    def __init__(self, config):
        self.conn = None
        self.database_path = pathlib.Path(config['database_path'])

    def open(self):
        self.conn = sqlite3.connect(str(self.database_path))
        self.cur = self.conn.cursor()
        self.ensure_index('ROMs', 'romHashSHA1')
        self.ensure_index('RELEASES', 'romID')

    def ensure_index(self, table, field):
        index_name = 'idx_' + table + '_' + field
        self.cur.execute("PRAGMA index_list('" + table + "')")
        indexes = [row[1] for row in self.cur.fetchall()]
        if index_name not in indexes:
            self.cur.execute("CREATE INDEX " + index_name + " ON " + table + " (" + field + ")")
    
    def rom_data(self, rom):
        self.cur.execute("SELECT romID, regionID FROM ROMs WHERE romHashSHA1 = '" + rom.sha1().upper() + "'")

        dbrom = self.cur.fetchone()
        if dbrom is None:
            return None

        return {
            'Year': '1982',
            'Genre': ['Action', '2D'],
            'Region': dbrom[1]
        }

    def close(self):
        if self.conn is not None:
            self.conn.close()
            self.conn = None
            self.cur = None