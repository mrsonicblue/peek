FROM misterkun/toolchain

# Eliminate bash prompt colors 
RUN set -ex; \
    rm -rf /root/.bashrc;

# Create build folder
RUN set -ex; \
    mkdir /build;

WORKDIR /build

# openssl
RUN set -ex; \
    wget --no-verbose https://github.com/openssl/openssl/archive/refs/tags/OpenSSL_1_0_2k.tar.gz; \
    tar xfz OpenSSL_1_0_2k.tar.gz; \
    rm OpenSSL_1_0_2k.tar.gz; \
    mv openssl-OpenSSL_1_0_2k openssl; \
    cd openssl; \
    ./config shared; \
    make;

# python make assumes the openssl libraries are in a 'lib' folder
RUN set -ex; \
    cd openssl; \
    mkdir lib; \
    cp *.so *.so.1.0.0 *.a *.pc lib/;

# # unrar
# RUN set -ex; \
#     wget --no-verbose http://http.us.debian.org/debian/pool/non-free/u/unrar-nonfree/unrar_5.6.6-1_armhf.deb; \
#     dpkg -i unrar_5.6.6-1_armhf.deb; \
#     rm unrar_5.6.6-1_armhf.deb;

# # mister environment
# RUN set -ex; \
#     wget --no-verbose https://github.com/MiSTer-devel/SD-Installer-Win64_MiSTer/raw/master/release_20210316.rar; \
#     unrar x -y release_20210316.rar files/linux/linux.img linux.img; \
#     rm release_20210316.rar;
#     # rm -rf linux;

# python3
RUN set -ex; \
    wget --no-verbose https://www.python.org/ftp/python/3.5.2/Python-3.5.2.tgz; \
    tar xfz Python-3.5.2.tgz; \
    rm Python-3.5.2.tgz; \
    mv Python-3.5.2 python3; \
    cd python3; \
    ./configure --disable-ipv6;

COPY python3.setup python3/Modules/Setup.local

# RUN set -ex; \
#     cd python3; \
#     OPENSSL_ROOT="/build/openssl" make;

# sqlite
RUN set -ex; \
    wget --no-verbose https://www.sqlite.org/src/tarball/sqlite.tar.gz?r=version-3.35.4 -O sqlite.tar.gz; \
    tar xfz sqlite.tar.gz; \
    rm sqlite.tar.gz; \
    cd sqlite; \
    ./configure; \
    make sqlite3.c;

# pysqlite3
RUN set -ex; \
    wget --no-verbose https://github.com/mrsonicblue/pysqlite3/archive/refs/tags/0.4.5-retro.tar.gz; \
    tar xfz 0.4.5-retro.tar.gz; \
    rm 0.4.5-retro.tar.gz; \
    mv pysqlite3-0.4.5-retro pysqlite3; \
    cd pysqlite3; \
    cp /build/sqlite/sqlite3.[ch] ./; \
    /build/py/bin/python3 setup.py build_static build;


# # sqlite
# RUN set -ex; \
#     wget --no-verbose https://www.sqlite.org/src/tarball/sqlite.tar.gz?r=version-3.35.4 -O sqlite.tar.gz; \
#     tar xfz sqlite.tar.gz; \
#     rm sqlite.tar.gz; \
#     cd sqlite; \
#     ./configure; \
#     make sqlite3.c;

# # pysqlite3
# RUN set -ex; \
#     wget --no-verbose https://github.com/mrsonicblue/pysqlite3/archive/refs/tags/0.4.5-retro.tar.gz; \
#     tar xfz 0.4.5-retro.tar.gz; \
#     rm 0.4.5-retro.tar.gz; \
#     mv pysqlite3-0.4.5-retro pysqlite3; \
#     cd pysqlite3; \
#     cp /build/sqlite/sqlite3.[ch] ./; \
#     /build/python3/python setup.py build_static build;




# FROM ubuntu:20.04

# # Build tools
# RUN set -ex; \
#     apt-get update ; \
#     DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends tzdata; \
#     apt-get install -y build-essential make python3 python3-setuptools tclsh wget xz-utils;

# # Create build folder
# RUN set -ex; \
#     mkdir /build;

# WORKDIR /build

# # gcc
# RUN set -ex; \
#     wget --no-verbose https://releases.linaro.org/components/toolchain/binaries/6.5-2018.12/arm-linux-gnueabihf/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz; \
#     tar xfJ gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz; \
#     rm gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz; \
#     mv gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf gcc;

# ENV PATH="${PATH}:/build/gcc/bin"

# # RUN set -ex; \
# #     wget --no-verbose https://github.com/LMDB/lmdb/archive/LMDB_0.9.28.tar.gz; \
# #     tar xfz LMDB_0.9.28.tar.gz; \
# #     rm LMDB_0.9.28.tar.gz; \
# #     mv lmdb-LMDB_0.9.28 lmdb; \
# #     cd lmdb/libraries/liblmdb; \
# #     make CC=/build/gcc/bin/arm-linux-gnueabihf-gcc AR=/build/gcc/bin/arm-linux-gnueabihf-ar liblmdb.a;

# # # libfuse
# # RUN set -ex; \
# #     wget --no-verbose https://github.com/libfuse/libfuse/archive/fuse-2.9.7.tar.gz; \
# #     tar xfz fuse-2.9.7.tar.gz; \
# #     rm fuse-2.9.7.tar.gz; \
# #     mv libfuse-fuse-2.9.7 libfuse; \
# #     cd libfuse; \
# #     ./makeconf.sh; \
# #     ./configure --prefix=/build/gcc --host=arm-linux-gnueabihf --disable-static; \
# #     make;

# # # libsqlite3
# # RUN set -ex; \
# #     wget --no-verbose https://sqlite.org/2021/sqlite-autoconf-3350400.tar.gz; \
# #     tar xfz sqlite-autoconf-3350400.tar.gz; \
# #     rm sqlite-autoconf-3350400.tar.gz; \
# #     mv sqlite-autoconf-3350400 sqlite3; \
# #     cd sqlite3; \
# #     ./configure  --prefix=/build/gcc --host=arm-linux-gnueabihf --disable-static --disable-threadsafe --disable-dynamic-extensions --disable-math --disable-fts4 --disable-fts3 --disable-fts5 --disable-json1 --disable-rtree; \
# #     make CFLAGS="-g -Os"; \
# #     make install-strip;

# # openssl
# RUN set -ex; \
#     wget --no-verbose https://github.com/openssl/openssl/archive/refs/tags/OpenSSL_1_0_2k.tar.gz; \
#     tar xvfz OpenSSL_1_0_2k.tar.gz; \
#     rm OpenSSL_1_0_2k.tar.gz; \
#     mv openssl-OpenSSL_1_0_2k openssl; \
#     cd openssl; \
#     export CROSS_COMPILE=arm-linux-gnueabihf-; \
#     ./Configure shared --prefix=/build/gcc --openssldir=/build/gcc linux-armv4; \
#     make;

# # # python3
# # RUN set -ex; \
# #     wget --no-verbose https://www.python.org/ftp/python/3.5.2/Python-3.5.2.tgz; \
# #     tar xfz Python-3.5.2.tgz; \
# #     rm Python-3.5.2.tgz; \
# #     mv Python-3.5.2 python3; \
# #     cd python3; \
# #     printf "ac_cv_file__dev_ptmx=no\nac_cv_file__dev_ptc=no" > config.site; \
# #     CONFIG_SITE=config.site ./configure  --prefix=/build/gcc --host=arm-linux-gnueabihf --build=arm --disable-ipv6; \
# #     # This fails, but makes it far enough to link against
# #     make || true;

# # # sqlite
# # RUN set -ex; \
# #     wget --no-verbose https://www.sqlite.org/src/tarball/sqlite.tar.gz?r=version-3.35.4 -O sqlite.tar.gz; \
# #     tar xfz sqlite.tar.gz; \
# #     ls -lah; \
# #     rm sqlite.tar.gz; \
# #     cd sqlite; \
# #     ./configure  --prefix=/build/gcc --host=arm-linux-gnueabihf; \
# #     make sqlite3.c;

# # # pysqlite3
# # RUN set -ex; \
# #     wget --no-verbose https://github.com/mrsonicblue/pysqlite3/archive/refs/tags/0.4.5-retro.tar.gz; \
# #     tar xfz 0.4.5-retro.tar.gz; \
# #     rm 0.4.5-retro.tar.gz; \
# #     mv pysqlite3-0.4.5-retro pysqlite3; \
# #     cd pysqlite3; \
# #     cp /build/sqlite/sqlite3.[ch] ./; \
# #     export ARCH=arm; \
# #     PLAT=arm-linux-gnueabihf-; \
# #     export _PYTHON_HOST_PLATFORM=linux-arm; \
# #     export CROSS_COMPILE=arm-linux-gnueabihf-; \
# #     export CC="${PLAT}gcc -pthread"; \
# #     export LDSHARED="${CC} -shared"; \
# #     export CROSSBASE="/build/python3"; \
# #     export CFLAGS="-I/build/python3 -I/build/python3/Include -I${CROSSBASE}/usr/include"; \
# #     export LDFLAGS="-L${CROSSBASE}/lib -L${CROSSBASE}/usr/lib"; \
# #     python3 setup.py build_static build; \
# #     arm-linux-gnueabihf-strip -s build/lib.linux-arm-3.8/pysqlite3/_sqlite3.cpython-38-x86_64-linux-gnu.so;

# WORKDIR /project