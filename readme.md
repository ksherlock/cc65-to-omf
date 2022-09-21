
## cc65 to omf

Convert [cc65](https://cc65.github.io) object files and libraries to OMF object files and libraries.

Why?  The original idea was to use ORCA/M or MPW Asm IIgs with
[ip65](https://github.com/cc65/ip65), so I don't have to learn ca65.  Of course, ca65 is a fine assembler and in some ways better...

## Conversion

* Imports -> N/A
* Exports -> OMF `GLOBAL` or `GEQU` entries
* ZEROPAGE segment -> OMF Stack segment
* other segments -> OMF Code segments

## Warning

cc65 object files allow you to import symbol then export it under a different name (and possibly as part of a larger expression).  This can (as of September 2022) cause problems with ORCA Linker.  Apple's linker (for APW or MPW) handles them better but there may still be issues.
