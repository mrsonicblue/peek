import sys

CRITICAL = 0
ERROR = 1
WARNING = 2
INFORMATION = 3
DEBUG = 4

this = sys.modules[__name__]
this.status = ""
this.core_index = 0
this.core_count = 0
this.core_name = ""
this.rom_index = 0
this.rom_count = 0
this.rom_name = ""

def _w(level, s, *args):
    if len(args) > 0:
        s = s.format(*args)

    status_erase()
    print(s)
    status_write()

def crit(s, *args):
    _w(CRITICAL, s, *args)

def error(s, *args):
    _w(ERROR, s, *args)

def warn(s, *args):
    _w(WARNING, s, *args)

def info(s, *args):
    _w(INFORMATION, s, *args)

def debug(s, *args):
    _w(DEBUG, s, *args)

def status_erase():
    l = len(this.status)
    print("\b" * l, end="")
    print(" " * l, end="")
    print("\b" * l, end="")
    pass

def status_write():
    print(this.status, end="", flush=True)

def status_update():
    p = 0
    if this.rom_count > 0:
        p = int(100 * this.rom_index / this.rom_count)
    s = "{}: {}%".format(this.core_name, p)
    if s != this.status:
        status_erase()
        this.status = s
        status_write()

def core(index, count, name):
    this.core_index = index
    this.core_count = count
    this.core_name = name
    status_update()

def rom(index, count, name):
    this.rom_index = index
    this.rom_count = count
    this.rom_name = name
    status_update()

status_update()