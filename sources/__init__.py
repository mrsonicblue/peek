from . import dummy
from . import openvgdb
from . import smdb

all = {
    "dummy": dummy.DummySource,
    "openvgdb": openvgdb.OpenVgdbSource,
    "smdb": smdb.SmdbSource
}