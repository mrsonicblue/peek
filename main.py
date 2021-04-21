#!/bin/env python3
import os
import json
import pathlib
from scanner import Scanner

app_path = pathlib.Path(__file__).resolve().parent.absolute()
config_path = app_path / 'config.json'

with open(str(config_path)) as f:
    config = json.load(f)

scanner = Scanner(config)
scanner.run()

# import requests

# r = requests.get('https://api.github.com/user', auth=('user', 'pass'))
# print(r.status_code)
# print(r.headers['content-type'])
# print(r.encoding)
# print(r.text)
# print(r.json())