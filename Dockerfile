FROM mrsonicblue/mister-build

# Create build folder
RUN set -ex; \
    mkdir /build;

WORKDIR /build

# liblmdb
RUN set -ex; \
    wget --no-verbose https://github.com/LMDB/lmdb/archive/LMDB_0.9.29.tar.gz; \
    tar xfz LMDB_0.9.29.tar.gz; \
    rm LMDB_0.9.29.tar.gz; \
    mv lmdb-LMDB_0.9.29 lmdb; \
    cd lmdb/libraries/liblmdb; \
    make CC=arm-linux-gnueabihf-gcc AR=arm-linux-gnueabihf-ar liblmdb.a;

# libfuse
RUN set -ex; \
    wget --no-verbose https://github.com/libfuse/libfuse/archive/fuse-2.9.7.tar.gz; \
    tar xfz fuse-2.9.7.tar.gz; \
    rm fuse-2.9.7.tar.gz; \
    mv libfuse-fuse-2.9.7 libfuse; \
    cd libfuse; \
    ./makeconf.sh; \
    ./configure --host=arm-linux-gnueabihf --disable-shared; \
    make;

WORKDIR /project