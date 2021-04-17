#!/bin/python3
import ssl
import sqlite3
import requests

conn = sqlite3.connect('openvgdb.sqlite')

cur = conn.cursor()
cur.execute("SELECT * FROM REGIONS")

rows = cur.fetchall()
for row in rows:
    print(row)

r = requests.get('https://api.github.com/user', auth=('user', 'pass'))
print(r.status_code)
print(r.headers['content-type'])
print(r.encoding)
print(r.text)
print(r.json())

print("WEE")