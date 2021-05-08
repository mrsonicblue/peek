import io
import log
import re
import requests
import secret

class ScreenScraperSource:
    def __init__(self, config):
        self.username = config['username']
        self.password = config['password']

    def open(self):
        s = requests.Session()
        s.params['devid'] = secret.xor(b'WTYeKgYQGTdZBys=', b'4DmEhyzU5rNMen2RWTqd')
        s.params['devpassword'] = secret.xor(b'NTkpHjIEKCM1ND8=', b'QRgqagGZbAs966Sxnkso')
        s.params['softname'] = 'Peek'
        s.params['ssid'] = self.username
        s.params['sspassword'] = self.password
        s.params['output'] = 'json'
        self.session = s

        self.ensure_creds()
        self.regions = self.get_regions()

    def close(self):
        self.session = None

    def ensure_creds(self):
        with self.session.get('https://www.screenscraper.fr/api2/ssuserInfos.php') as r:
            r.raise_for_status()

            data = r.json()
            if data['response']['ssuser']['id'] != self.username:
                raise Exception('Credentials seem invalid')

    def get_regions(self):
        with self.session.get('https://www.screenscraper.fr/api2/regionsListe.php') as r:
            r.raise_for_status()

            data = r.json()
            result = {}
            for region_id, region in data['response']['regions'].items():
                if 'nom_en' in region:
                    result[region['nomcourt']] = region['nom_en']

            return result

    def core_start(self, core):
        pass

    def core_end(self):
        pass

    def rom_data(self, rom):
        params = {
            'crc':rom.crc32(),
            'md5':rom.md5(),
            'sha1':rom.sha1(),
            'romtype':'rom',
            'romnom':rom.name,
            'romtaille':str(rom.stat.st_size)
        }
        log.info(params)
        with self.session.get('https://www.screenscraper.fr/api2/jeuInfos.php', params=params) as r:
            # Rom not found
            if r.status_code == 404:
                return {}

            r.raise_for_status()

            result = {}

            data = r.json(object_pairs_hook=ScreenScraperSource.handle_duplicates)
            response = data['response']
            game = response['jeu']

            region_keys = self.try_path(game, ['rom', 'romregions'])
            if region_keys:
                region_keys = region_keys.split(',')
                regions = []
                for region_key in region_keys:
                    if region_key in self.regions:
                        regions.append(self.clean(self.regions[region_key]))

                if len(regions) > 0:
                    result['Region'] = regions

            genres = []
            for genre_info in game.get('genres', []):
                genre = self.clean(self.try_path(genre_info, ['noms', ('langue','en'), 'text']))
                if genre:
                    genres.append(genre)

            if len(genres) > 0:
                result['Genre'] = genres

            publisher = self.clean(self.try_path(game, ['editeur', 'text']))
            if publisher:
                result['Publisher'] = publisher

            developer = self.clean(self.try_path(game, ['developpeur', 'text']))
            if developer:
                result['Developer'] = developer

            players = self.clean(self.try_path(game, ['joueurs', 'text']))
            if players:
                result['Players'] = players

            date = self.clean(self.try_path(game, ['rom', 'dates', 'date_us']))
            if date:
                m = re.search('([0-9]{4})', date)
                if m is not None:
                    result['Year'] = m.group(1)

            rating = self.clean(self.try_path(game, ['classifications', ('type','ESRB'), 'text']))
            if rating:
                result['Rating'] = rating

            log.info(result)

            return result

    def clean(self, v):
        if not v or type(v) is not str:
            return v
        
        return v.replace('/', '-')

    def try_path(self, d, path):
        for bit in path:
            if type(d) is list:
                if type(bit) is tuple:
                    d = next((item for item in d if type(item) is dict and item.get(bit[0], None) == bit[1]), None)
                else:
                    return None
            elif type(d) is dict:
                d = d.get(bit, None)
            else:
                return None

        return d

    def force_list(self, v):
        if type(v) is list:
            return v
        return [v]

    @staticmethod
    def handle_duplicates(ordered_pairs):
        d = {}
        for k, v in ordered_pairs:
            if k in d:
                if type(d[k]) is list:
                    d[k].append(v)
                else:
                    d[k] = [d[k],v]
            else:
                d[k] = v
        return d