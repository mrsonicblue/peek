import pathlib
import sqlite3
import re

class OpenVgdbSource:
    def __init__(self, config):
        self.conn = None
        self.database_path = pathlib.Path(config['database_path'])

    def open(self):
        self.conn = sqlite3.connect(str(self.database_path))
        self.cur = self.conn.cursor()
        self.ensure_index('ROMs', 'romHashSHA1')
        self.ensure_index('RELEASES', 'romID')
        self.regions = self.get_regions()
            
    def get_regions(self):
        self.cur.execute('SELECT RegionID, RegionName FROM REGIONS')
        return { row[0]:self.parse_list(row[1]) for row in self.cur.fetchall() }

    def parse_list(self, s):
        bits = s.split(',')
        bits = map(lambda b: b.strip(), bits)
        return list(bits)

    def ensure_index(self, table, field):
        index_name = 'idx_' + table + '_' + field
        self.cur.execute("PRAGMA index_list('" + table + "')")
        indexes = [row[1] for row in self.cur.fetchall()]
        if index_name not in indexes:
            self.cur.execute("CREATE INDEX " + index_name + " ON " + table + " (" + field + ")")
    
    def rom_data(self, rom):
        self.cur.execute('SELECT romID, regionID FROM ROMs WHERE romHashSHA1 = ?', (rom.sha1().upper(),))
        dbrom = self.cur.fetchone()
        if dbrom is None:
            return {}

        self.cur.execute('SELECT releaseDeveloper, releaseGenre, releaseDate FROM RELEASES WHERE romID = ?', (dbrom[0],))
        dbrelease = self.cur.fetchone()
        if dbrelease is None:
            return {}

        year = None
        if dbrelease[2] is not None:
            match = re.search('([0-9]{4})', dbrelease[2])
            if match is not None:
                year = match.group(1)

        genre = None
        if dbrelease[1] is not None:
            genre = self.parse_list(dbrelease[1])

        return {
            'Year': year,
            'Developer': dbrelease[0],
            'Genre': genre,
            'Region': self.regions.get(dbrom[1], None)
        }

    def close(self):
        if self.conn is not None:
            self.conn.close()
            self.conn = None
            self.cur = None