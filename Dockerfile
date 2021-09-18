FROM misterkun/toolchain

# Eliminate bash prompt colors 
RUN set -ex; \
    rm -rf /root/.bashrc;

# Create build folder
RUN set -ex; \
    mkdir /build;

WORKDIR /build

# SQLite
RUN set -ex; \
    wget --no-verbose https://www.sqlite.org/src/tarball/sqlite.tar.gz?r=version-3.35.4 -O sqlite.tar.gz; \
    tar xfz sqlite.tar.gz; \
    rm sqlite.tar.gz; \
    cd sqlite; \
    ./configure; \
    make sqlite3.c;

# Python 3
RUN set -ex; \
    wget --no-verbose https://www.python.org/ftp/python/3.9.6/Python-3.9.6.tgz; \
    tar xfz Python-3.9.6.tgz; \
    rm Python-3.9.6.tgz; \
    mv Python-3.9.6 python3; \
    cd python3; \
    ./configure --prefix=/build/python3-install --disable-ipv6; \
    make; \
    make install;

# _sqlite3 module
COPY _sqlite3 _sqlite3
RUN set -ex; \
    cd _sqlite3; \
    /build/python3-install/bin/python3 setup.py install;

# Additional pip modules
RUN set -ex; \
    /build/python3-install/bin/python3 -m ensurepip; \
    /build/python3-install/bin/python3 -m pip install requests;

WORKDIR /project