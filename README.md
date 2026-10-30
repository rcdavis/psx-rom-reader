# psx-rom-reader
Reads assets in a PSX rom. The repository is organized as a CMake superproject containing a dependency-free C99 library (`lib/`) and a command line viewer (`viewer/`).

Supports `.iso` (2048) and raw `.bin`/`.img` (2352 / 2336 byte sectors) images, ISO9660 browsing/extraction, TIM texture decoding, CD-XA ADPCM audio decoding, and STR movie (MDEC v2/v3) frame decoding.

XA audio needs a raw image (`.bin`) because plain `.iso` files drop the Mode 2 subheaders. STR video also works on `.iso`.

## Build everything
```sh
cmake -S . -B build
cmake --build build
```

The root project builds the library and viewer. To configure only the library from the repository root, pass `-DPSXROM_BUILD_VIEWER=OFF`.

## Use the library via CMake FetchContent
```cmake
include(FetchContent)
FetchContent_Declare(psxrom
    GIT_REPOSITORY https://github.com/rcdavis/psx-rom-reader.git
    GIT_TAG main)
FetchContent_MakeAvailable(psxrom)
target_link_libraries(my_app PRIVATE psxrom::psxrom)
```
When fetched as a dependency, the viewer is disabled by default.

The library can also be configured directly with `cmake -S lib -B build-lib`. For development, the viewer's CMake project can be configured independently with `cmake -S viewer -B build-viewer`; it uses the sibling `lib/` project.

```c
#include <psxrom/psxrom.h>
psxrom_disc *d; psxrom_disc_open("game.bin", &d);
psxrom_entry e; psxrom_find(d, "/SYSTEM.CNF", &e);
uint8_t *data; size_t n; psxrom_read_file(d, &e, &data, &n);
```

## Viewer commands
```
build/psxview game.bin info
build/psxview game.bin ls
build/psxview game.bin tims
build/psxview game.bin hex /SYSTEM.CNF
build/psxview game.bin extract /DIR/FILE.TIM out.tim
build/psxview game.bin extract-all out/
build/psxview game.bin tim /DIR/ICON.TIM icon.bmp [palette]
build/psxview game.bin tim-all textures/
build/psxview game.bin xa-list /MOVIE/OP1.STR          # XA audio streams (file/channel)
build/psxview game.bin xa /MOVIE/OP1.STR 1 1 out.wav    # decode one stream to WAV
build/psxview game.bin xa-all /MOVIE/OP1.STR audio/
build/psxview game.bin str-info /MOVIE/OP1.STR
build/psxview game.bin str-frame /MOVIE/OP1.STR 100 frame.bmp
build/psxview game.bin str-frames /MOVIE/OP1.STR frames/ [first] [count]
build/psxview game.bin str-y4m /MOVIE/OP1.STR op1.y4m       # uncompressed YUV4MPEG2 video, no ffmpeg needed
build/psxview game.bin str-convert /MOVIE/OP1.STR op1.webm  # video + XA audio; format chosen by extension
```

`str-convert` pipes frames into `ffmpeg` (must be in `PATH`) and muxes the first XA audio stream found in the movie, offset by its sector distance from the first video frame. Y4M files are large (about 300 MB for a two-minute movie); the frame rate is derived from sector spacing since STR files don't store it.

## Library API (media)
```c
psxrom_xa_scan / psxrom_xa_decode   /* CD-XA ADPCM -> interleaved 16-bit PCM */
psxrom_str_open / psxrom_str_next_frame / psxrom_mdec_decode   /* STR -> RGBA frames */
psxrom_disc_read_raw                /* Mode 2 subheader + payload of a sector */
```
Notes: MDEC bitstream version 3 was verified against a real disc; version 2 follows the hardware layout but is untested. Decoding uses a floating-point IDCT, so output is visually correct but not bit-exact with the console.
