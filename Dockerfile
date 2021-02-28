FROM ubuntu:20.04

# Build tools
RUN set -ex; \
    apt-get update && apt-get install -y make wget xz-utils;

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
    make CC=/build/gcc/bin/arm-linux-gnueabihf-gcc AR=/build/gcc/bin/arm-linux-gnueabihf-ar liblmdb.a; \
    mkdir -p /build/gcc/arm-linux-gnueabihf/include/libusb-1.0; \
    cp lmdb.h /build/gcc/arm-linux-gnueabihf/include/; \
    cp liblmdb.a /build/gcc/lib/gcc/arm-linux-gnueabihf/6.5.0/;

WORKDIR /project