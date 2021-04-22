from . import dummy
from . import openvgdb

all = {
    "dummy": dummy.DummySource,
    "openvgdb": openvgdb.OpenVgdbSource
}