apt-get install nano

git clone --depth=1 https://github.com/MiSTer-devel/Linux-Kernel_MiSTer.git
cd Linux-Kernel_MiSTer

edit arch/arm/configs/MiSTer_defconfig
enable CONFIG_USB_ACM

make MiSTer_defconfig
make -j6 zImage
make socfpga_cyclone5_de10_nano.dtb
cat arch/arm/boot/zImage arch/arm/boot/dts/socfpga_cyclone5_de10_nano.dtb > zImage_dtb

docker cp 403a2267be04:/project/Linux-Kernel_MiSTer/zImage_dtb zImage_dtb