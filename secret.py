import base64
from itertools import cycle

def xor(data, key):
    data = base64.decodestring(data)
    return ''.join(chr(x ^ y) for (x,y) in zip(data, cycle(key)))