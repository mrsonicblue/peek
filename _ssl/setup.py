from distutils.core import setup, Extension

module = Extension('_ssl',
    include_dirs = ['/usr/local/include', '/build/openssl/include'],
    libraries = ['crypto','ssl'],
    library_dirs = ['/usr/local/lib', '/build/openssl'],
    sources = ['/build/python3/Modules/_ssl.c'])

setup (name = 'Ess Ess Ell',
    version = '3.5.2',
    description = 'SSL moved out',
    author = 'Eric Schneider',
    author_email = 'mr.sonicblue@gmail.com',
    url = 'https://www.python.org/',
    long_description = 'Hi mom!',
    ext_modules = [module])