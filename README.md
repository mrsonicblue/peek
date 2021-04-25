# Peek filtering

This project adds filtering for ROM files using a virtual filesystem. When mounted to a folder, filtering is provided 
for the files in the parent folder. Built-in filters include first-letter filtering, recently played, and favorites. 
Additional facet filtering can be added by importing metadata.

This project and documentation assumes you are using Peek for the MiSTer platform, but this code will work for filtering 
arbitrary files with only minor modification.

## Getting started 

An (experimental) install script is available. This will download the latest version and configure itself to
automatically run on boot.

To run the install script, open terminal with F9 (or connect with SSH) and run:

```
bash <(curl -ks https://raw.githubusercontent.com/mrsonicblue/peek/master/install.sh)
```

If this is successful, you will still need to [import](#import) data to make the [facet filter](#facet-filter) work.
A simple [scanner](https://github.com/mrsonicblue/peek-scan-2) is bundled in to help simplify this process.

If you wish to build the application from source or if you wish to customize it, continue on through this section.

### Building

Docker can be used on any platform to build:

```
docker-compose run --rm build make
```

This will produce two executable binaries: `peek` and `peekfs`

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

### Service

In addition to hosting some [utility commands](#utility-commands), the `peek` command is a service which monitors 
for cores and ROMs loaded by MiSTer. Although the [service script](#installing) is recommended, the service can be 
run manually:

```
./peek
```

When a core is loaded, a subfolder named `Peek` is created and mounted to. When a new core is loaded, the previous mount 
is unmounted. The subfolder is not deleted to prevent unnecessary writes on the SD card.

When a rom is loaded, it is automatically added to the recently played filter. See [filters](#filters).

**Note:** The service currently tries to connect a serial device. This is meant to provide control from an
external microcontroller. This feature is not yet documented. You will see an error every 10 seconds because
the connection to that device will fail.

### Uninstalling

To stop the Peek service:

```
/etc/init.d/S99peek stop
```

To remove Peek completely:

```
rm -rf /etc/init.d/S99peek /media/fat/peek
```

## Filters

Most filters rely on data stored in a database maintained by the `peek` command. The database is a simple key/value
store. The keys are structured to support the filters and the values are (typically) filenames. Because ROM files
are split between different folders per core, the keys are also organized by core. The core name is extracted from
the mount path. For example, when mounted at `/media/fat/games/NES/Peek` the core is assumed to be `NES`. In the 
notes for each filter, `CORENAME` is used as a placeholder for this value.

Files displayed in a filter act as a passthrough to the actual file in the parent directory of the mount. Because of
this, any filename referenced in the database which does not match an actual file will be excluded from the file 
listing.

### First-letter filter

A subfolder is created for each letter of the alphabet and `0-9`. Each folder display files with a filename starting
with the same first character. Despite the name, the `0-9` folder will display files starting with any character not
between A and Z. This filter does not reply on an data.

See [utility commands](#utility-commands) for details on how to manage and import data.

### Favorites filter

Files are displayed which match the key `fav/CORENAME`.

### Recently played filter

Files are displayed which match the key `rec/CORENAME`. The value for this filter is prefixed with 8 bytes which are
ignored. The prefix is a timestamp used to automatically remove files after a maximum of 20.

### Facet filter

Additional folders are created for keys matching the mattern `has/CORENAME/ONE/TWO`. `ONE` is the top level folder and 
`TWO` is the second level folder. This filter currently only supports exactly two levels (neither less nor more).
Given the following list of keys:

```
has/NES/Year/1982
has/NES/Year/1983
has/NES/Year/1984
```

A top level folder `Year` will appear with subdirectories for `1982`, `1983`, and `1984`. The same key would be used
for all of the files with the same facet.

## Utility Commands

The `peek` command provides utilities to manage filter data.

### Get

Usage: `peek db get [KEY]`

When `KEY` is provided, all data with that key is shown. When `KEY` is omitted, all data is shown.

Example: `peek db get fav/NES`

### Get prefix

Usage: `peek db getpre PREFIX`

All data is shown with a key that begins with `PREFIX`.

Example: `peek db getpre has/NES/`

### Get slice

Usage: `peek db getsli PREFIX`

Keys are searched which begin with `PREFIX`. A value is extract between the prefix and the next `/`.
A distinct list of the extracted values is show. This simulates how folders are generated for the
facet filter.

Example: `peek db getsli has/NES/`

### Get keys

Usage: `peek db getkeys PREFIX`

All keys are returned for given `VALUE`.

Example: `peek db getkeys "Some Game.nes"`

### Put

Usage: `peek db put KEY VALUE`

A record is created with the given `KEY` and `VALUE`, unless it already exists.

Example: `peek db put fav/NES "Some Game.nes"`

### Delete

Usage: `peek db del KEY [VALUE]`

Deletes records with the provided `KEY`. When `VALUE` is provide, only the record with the matching
value is deleted. When `VALUE` is omitted, all records with the matching key are deleted.

Example: `peek db del fav/NES "Some Game.nes"`

### Delete prefix

Usage: `peek db delpre PREFIX`

Identical to [get prefix](#get-prefix), except matching records are deleted. This is useful for
clearing out [imported](#import) data.

Example: `peek db delpre has/NES/`

### Import

Usage: `peek db import CORENAME FILE`

Import a tab-delimited file for the [facet filter](#facet-filter). `CORENAME` is used to generate
keys for the appropriate core. The file to import is provided with `FILE`. The first column in the file 
must be a filename. All remaining columns are assumed to be filters. The first line of the file is used 
as a header and is used as the top-level facet value. The associated values in each column are the 
second-level facet value. To provide multiple second-level facets for the same file, delimit the values 
with `|`. Given this file:

```
ROM     Region     Year Genre
One.nes USA|Europe 1982 Action
Two.nes USA        1984 Sports|Basketball
```

**Note:** this example is space delimited due to limitations of the documentation, but it must
be tab-delimited.

These records will be created:

```
has/NES/Region/USA       -> One.nes
has/NES/Region/Europe    -> One.nes
has/NES/Region/USA       -> Two.nes
has/NES/Year/1982        -> One.nes
has/NES/Year/1984        -> Two.nes
has/NES/Genre/Action     -> One.nes
has/NES/Genre/Sports     -> Two.nes
has/NES/Genre/Basketball -> Two.nes
```

Example: `peek db import NES NES.txt`

A reference project to generate this format is available [here](https://github.com/mrsonicblue/peek-scan).
