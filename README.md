# Peek filtering

This project adds filtering for ROM files using a virtual filesystem. When mounted to a folder, filtering is provided 
for the files in the parent folder. Built-in filters include first-letter filtering, recently played, and favorites. 
Additional facet filtering can be added by importing metadata.

This project and documentation assumes you are using Peek for the MiSTer platform, but this code will work for filtering 
arbitrary files with only minor modification.

## Getting started 

To get started, a few steps are required.

### Building

Binary releases are not currently available. Docker can be used on any platform to build:

```
docker-compose run --rm build make
```

This will produce to executable binaries: `peek` and `peekfs`

### Installing

To install, copy the built binaries to your MiSTer. It's recommended that you use a folder on the SD card at
`/media/fat/peek` as the installation folder. If copying from a non-Linux platform, you may need
to make the binaries executable:

```
chmod +x peek peekfs
```

The service script `S99peek` is included if you'd like `peek` to start automatically on boot. The script assumes
you installed the binaries in `/media/fat/peek` and requires modification for any other path. To install:

```
chmod +x S99peek
cp S99peek /etc/init.d/
```

### Mounting

The `peekfs` command is used to mount filters to an empty folder. For example:

```
mkdir /media/fat/games/NES/Peek
./peekfs /media/fat/games/NES/Peek
```

If you navigate to the mount folder, you will see subdirectories for each filter. To unmount:

```
unmount /media/fat/games/NES/Peek
```
