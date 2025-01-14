# ld64

This repo is a mirror of ld64 source dumps from opensource.apple.com.
This is useful for diffing changes between Xcode versions and building
the linker yourself locally.

NOTE: As of Xcode 15 Apple launched a new linker which is not open
source. This source code corresponds to the legacy version which you get
by passing `-ld_classic` to the linker that ships with Xcode.

## Building

If you would like to build ld64 yourself, which can be useful for
debugging linker issues, you can use these branches depending on what
ld64 version you want to test:

- [buildable-16.0](https://github.com/keith/ld64/tree/buildable-16.0)
- [buildable-15.3](https://github.com/keith/ld64/tree/buildable-15.3)
- [buildable-15.0](https://github.com/keith/ld64/tree/buildable-15.0)
- [buildable-13.2.1](https://github.com/keith/ld64/tree/buildable-13.2.1)
- [buildable-12.0](https://github.com/keith/ld64/tree/buildable-12.0)

## Updating this repo

```sh
./update.sh URL_OF_TAR_GZ
```
