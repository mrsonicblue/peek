#!/bin/env python3
import os
import json
import pathlib
from scanner import Scanner

app_path = pathlib.Path(__file__).resolve().parent.absolute()
config_path = app_path / 'config.json'

with config_path.open('r', encoding='utf-8') as f:
    config = json.load(f)

scanner = Scanner(config)
scanner.run()