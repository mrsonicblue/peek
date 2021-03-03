FROM ubuntu:20.04

# Build tools
RUN set -ex; \
    apt-get update ; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends tzdata; \
    apt-get install -y make libtool gettext wget xz-utils;

# Create build folder
RUN set -ex; \
    mkdir /build;

WORKDIR /build

# gcc
RUN set -ex; \
    wget --no-verbose https://releases.linaro.org/components/toolchain/binaries/6.5-2018.12/arm-linux-gnueabihf/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz; \
    tar xfJ gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz; \
    rm gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz; \
    mv gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf gcc;

ENV PATH="${PATH}:/build/gcc/bin"

# liblmdb
RUN set -ex; \
    wget --no-verbose https://github.com/LMDB/lmdb/archive/LMDB_0.9.28.tar.gz; \
    tar xfz LMDB_0.9.28.tar.gz; \
    rm LMDB_0.9.28.tar.gz; \
    mv lmdb-LMDB_0.9.28 lmdb; \
    cd lmdb/libraries/liblmdb; \
    make CC=/build/gcc/bin/arm-linux-gnueabihf-gcc AR=/build/gcc/bin/arm-linux-gnueabihf-ar liblmdb.a;

# libfuse
RUN set -ex; \
    wget --no-verbose https://github.com/libfuse/libfuse/archive/fuse-2.9.7.tar.gz; \
    tar xfz fuse-2.9.7.tar.gz; \
    rm fuse-2.9.7.tar.gz; \
    mv libfuse-fuse-2.9.7 libfuse; \
    cd libfuse; \
    ./makeconf.sh; \
    ./configure --prefix=/build/gcc --host=arm-linux-gnueabihf --disable-static; \
    make;

WORKDIR /project