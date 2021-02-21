FROM ubuntu:18.04

# Build tools
RUN set -ex; \
    apt-get update && apt-get install -y \
        build-essential \
        git \
        libncurses-dev \
        flex \
        bison \
        openssl \
        libssl-dev \
        dkms \
        libelf-dev \
        libudev-dev \ 
        libpci-dev \
        libiberty-dev \
        autoconf \
        liblz4-tool \
        bc \
        curl \
        gcc \
        git \
        libssl-dev \
        libncurses5-dev \
        lzop \
        make \
        u-boot-tools \
        wget \
        xz-utils;

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

#ENV PATH="${PATH}:/build/gcc/bin"
ENV ARCH=arm
ENV CROSS_COMPILE=/build/gcc/bin/arm-linux-gnueabihf-

WORKDIR /project