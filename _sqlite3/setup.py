from distutils.core import setup, Extension

module = Extension('_sqlite3',
    define_macros = [('MODULE_NAME', '"_sqlite3"')],
    include_dirs = ['/usr/local/include','/build/sqlite'],
    library_dirs = ['/usr/local/lib'],
    sources = [
        '/build/python3/Modules/_sqlite/cache.c',
        '/build/python3/Modules/_sqlite/connection.c',
        '/build/python3/Modules/_sqlite/cursor.c',
        '/build/python3/Modules/_sqlite/microprotocols.c',
        '/build/python3/Modules/_sqlite/module.c',
        '/build/python3/Modules/_sqlite/prepare_protocol.c',
        '/build/python3/Modules/_sqlite/row.c',
        '/build/python3/Modules/_sqlite/statement.c',
        '/build/python3/Modules/_sqlite/util.c',
        '/build/sqlite/sqlite3.c'
    ])

setup (name = 'See Quill Lite Three',
    version = '3.5.2',
    description = 'SQLite moved out',
    author = 'Eric Schneider',
    author_email = 'mr.sonicblue@gmail.com',
    url = 'https://www.python.org/',
    long_description = 'Hi mom!',
    ext_modules = [module])