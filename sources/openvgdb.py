import io
import log
import pathlib
import re
import requests
import shutil
import sqlite3
import zipfile

class OpenVgdbSource:
    def __init__(self, config):
        self.conn = None
        self.database_path = pathlib.Path(config['database_path'])

    def open(self):
        self.ensure_database()
        self.conn = sqlite3.connect(str(self.database_path))
        self.cur = self.conn.cursor()
        self.ensure_index('ROMs', 'romHashSHA1')
        self.ensure_index('RELEASES', 'romID')
        self.regions = self.get_regions()

    def close(self):
        if self.conn is not None:
            self.conn.close()
            self.conn = None
            self.cur = None

    def get_regions(self):
        self.cur.execute('SELECT RegionID, RegionName FROM REGIONS')
        return { row[0]:self.parse_list(row[1]) for row in self.cur.fetchall() }

    def parse_list(self, s):
        bits = s.split(',')
        bits = map(lambda b: b.strip(), bits)
        return list(bits)

    def get_database_url(self):
        with requests.get('https://api.github.com/repos/OpenVGDB/OpenVGDB/releases/latest') as r:
            r.raise_for_status()

            release = r.json()
            if not release:
                raise Exception('Failed to get release data')

            assets = release.get('assets', None)
            if not assets:
                raise Exception('Release does not contain any assets')
            
            assets = list(filter(lambda a: a['name'] == 'openvgdb.zip', assets))
            if len(assets) == 0:
                raise Exception('Release does not contain openvgdb.zip')

            url = assets[0].get('browser_download_url', None)
            if not url:
                raise Exception('Release does not contain download url')

            return url

    def ensure_database(self):
        if self.database_path.is_file():
            return

        log.info('Downloading OpenVGDB')

        url = self.get_database_url()
        with requests.get(url) as r:
            r.raise_for_status()
            with io.BytesIO() as mem:
                mem.write(r.content)

                log.info('Extracting OpenVGDB')

                with zipfile.ZipFile(mem) as zip:
                    with zip.open('openvgdb.sqlite') as zf, open(str(self.database_path), 'wb') as f:
                        shutil.copyfileobj(zf, f)

    def ensure_index(self, table, field):
        index_name = 'idx_' + table + '_' + field
        self.cur.execute("PRAGMA index_list('" + table + "')")
        indexes = [row[1] for row in self.cur.fetchall()]
        if index_name not in indexes:
            log.info('Creating database index {}', index_name)
            self.cur.execute("CREATE INDEX " + index_name + " ON " + table + " (" + field + ")")

    def core_start(self, core):
        pass

    def core_end(self):
        pass

    def rom_data(self, rom):
        self.cur.execute('SELECT romID, regionID FROM ROMs WHERE romHashSHA1 = ?', (rom.sha1(False).upper(),))
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