from . import dummy
from . import openvgdb
from . import screenscraper
from . import smdb

all = {
    "dummy": dummy.DummySource,
    "openvgdb": openvgdb.OpenVgdbSource,
    "screenscraper": screenscraper.ScreenScraperSource,
    "smdb": smdb.SmdbSource
}