# Peek reference scanner

The [Peek](https://github.com/mrsonicblue/peek) filtering project relies on data to provide filtering. This data
cannot be shipped with a release because it will be different for each user. This reference scanner offers a
straightforward way to scan ROM files and extract metadata from several sources. Releases for this project are
bundled into Peek.

## Getting started

Install [Peek](https://github.com/mrsonicblue/peek) using its install script. A copy of the scanner is bundled in.

You can then run the scanner by running:

```
/media/fat/peek/scan
```

Any scan source which works without additional configuration is enabled by default.

## Configuration

All configuration for the scanner is defined in the `config.json` file. By default, this file is located at
`/media/fat/peek/scan-inc/config.json`. You can edit the file with nano:

```
nano /media/fat/peek/scan-inc/config.json
```

The `general` configuration section defines the following settings:

* `log_level`: The amount of detail to include in the logs. Levels are `0` (critical), `1` (error), `2` (warning), `3` (information), `4` (debug).
* `games_path`: Absolute path to MiSTer games folder.
* `meta_path`: Absolute path to meta file storage.
* `tabs_path`: Absolute path to tabs file storage.
* `peek_path`: Absolute path to peek installation.
* `tab_headers`: Array of headers to include in tabs output.
* `core_white_list`: Array of cores to explicitly include in scan.
* `core_black_list`: Array of cores to explicitly exclude from scan.
* `rom_max_size`: ROM files larger than this value, in bytes, are skipped. Useful for skipping hard drive images.

The remaining configuration sections each correspond to a source. See [sources](#sources) for configuration 
details for each source.

## Sources

The scanner supports multiple sources to gather metadata for ROMs. This is useful because no source is perfect.
Some have more coverage for ROMs. Some have unique data. Some require less set up to use.

Each source has its own configuration section. All contain the following settings:

* `enabled`: Enables or disables the source
* `priority`: Controls the priority for duplicate values returned by multiple sources

### ScreenScraper

TODO

### OpenVGDB

TODO

### SMDB

TODO

### Dummy

A source which does nothing. The dummy source can be used as a starting point for a new source.

## Strategy

Each enabled source is opened at the beginning of the scan and closed at the end. This allows each source to
perform any set up or tear down. For example, some sources download data files or open databases. Each source 
is similarly notified when the scan for a core begins and ends.

For each ROM, each source is asked to return metadata. The source returns whatever data is available in the source,
even if it is nothing. If the source wants to retry the same ROM later, it can return `None`. This is useful for
API-based sources which have rate limiting. The source will retry skipped ROMs on subsequent scans.

Each source is configured with a relative priority level. The metadata for each source is overlayed based on the
priority. For example, if multiple sources return a `Genre` value, the value from the source with the highest priority
is used.

A per-core tab-delimited file is generated into the `tabs` folder. That file is imported into the Peek database.

*Note*: the scanner does not remove previously imported data from the Peek database. If the genre for a game changes,
both the old and new values will be in the database. Consult the documentation for
[Peek](https://github.com/mrsonicblue/peek) to learn how to manage the Peek database.
