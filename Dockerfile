FROM misterkun/toolchain

# Eliminate bash prompt colors 
RUN set -ex; \
    rm -rf /root/.bashrc;

# Create build folder
RUN set -ex; \
    mkdir /build;

WORKDIR /build

# OpenSSL
RUN set -ex; \
    wget --no-verbose https://github.com/openssl/openssl/archive/refs/tags/OpenSSL_1_0_2k.tar.gz; \
    apt remove -y libssl-dev libssl1.0.2; \
    tar xfz OpenSSL_1_0_2k.tar.gz; \
    rm OpenSSL_1_0_2k.tar.gz; \
    mv openssl-OpenSSL_1_0_2k openssl; \
    cd openssl; \
    ./config shared; \
    make; \
    make install;

# Link OpenSSL to standard lib location
RUN set -ex; \
    ln -s /usr/local/ssl/lib/libcrypto.so.1.0.0 /usr/lib/arm-linux-gnueabihf/libcrypto.so.1.0.0; \
    ln -s /usr/local/ssl/lib/libssl.so.1.0.0 /usr/lib/arm-linux-gnueabihf/libssl.so.1.0.0;

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
    wget --no-verbose https://www.python.org/ftp/python/3.5.2/Python-3.5.2.tgz; \
    tar xfz Python-3.5.2.tgz; \
    rm Python-3.5.2.tgz; \
    mv Python-3.5.2 python3; \
    cd python3; \
    ./configure --prefix=/build/python3-install --disable-ipv6; \
    make; \
    make install;

# # _ssl module
# COPY _ssl _ssl
# RUN set -ex; \
#     cd _ssl; \
#     /build/python3-install/bin/python3 setup.py install;

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