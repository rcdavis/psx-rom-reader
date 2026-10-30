# psx-rom-reader
Reads assets in a PSX rom. The repository is organized as a CMake superproject containing a dependency-free C99 library (`lib/`) and a command line viewer (`viewer/`).

Supports `.iso` (2048) and raw `.bin`/`.img` (2352 / 2336 byte sectors) images, ISO9660 browsing/extraction, TIM texture decoding, CD-XA ADPCM audio decoding, STR movie (MDEC v2/v3) frame decoding, and TMD 3D model parsing with glTF/GLB/OBJ export.

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
build/psxview game.bin tmd /DOOR/DOOR00.TMD door.glb      # summary + export; format by extension: .glb .gltf .obj
build/psxview game.bin tmd-all models/ [glb|gltf|obj]      # every .TMD on the disc (default glb)
build/psxview game.bin mmd-list                              # Digimon World models (US SLUS_010.32 only)
build/psxview game.bin mmd AGUM agumon.glb                   # posed + textured model, by name or index
build/psxview game.bin mmd-all models/ [glb|gltf|obj]        # every Digimon model
```

`str-convert` pipes frames into `ffmpeg` (must be in `PATH`) and muxes the first XA audio stream found in the movie, offset by its sector distance from the first video frame. Y4M files are large (about 300 MB for a two-minute movie); the frame rate is derived from sector spacing since STR files don't store it.

## Library API (media)
```c
psxrom_xa_scan / psxrom_xa_decode   /* CD-XA ADPCM -> interleaved 16-bit PCM */
psxrom_str_open / psxrom_str_next_frame / psxrom_mdec_decode   /* STR -> RGBA frames */
psxrom_disc_read_raw                /* Mode 2 subheader + payload of a sector */
```
Notes: MDEC bitstream version 3 was verified against a real disc; version 2 follows the hardware layout but is untested. Decoding uses a floating-point IDCT, so output is visually correct but not bit-exact with the console.

### TMD notes

`psxrom_tmd_parse` handles standard TMD files (id `0x41`, non-fixed addresses): flat/Gouraud, textured/untextured triangles and quads (quads are split in two). Lines and sprites are skipped. Export (glTF 2.0 `.glb`/`.gltf`, or OBJ) negates Y and Z so models are upright, with one mesh per TMD object carrying positions, normals, vertex colours and UVs. `.gltf` embeds its data as a base64 URI. UVs are per texture page; no VRAM texturing or materials are produced.

### Digimon World `.MMD` models

An MMD is `[tmd offset = 8][animation offset]`, then a standard TMD, then an animation section. The TMD holds one object per body part; the skeleton (which object each node draws and its parent) and the bone counts live in tables in the US executable, so `psxrom_dw1_load_digimon` reads `SLUS_010.32` and other regional executables are not supported. Textures are the matching 0x4800-byte 4bpp TIM in `/CHDAT/ALLTIM.TIM`; a face's palette is its CLUT row minus the TIM's CLUT row.

`psxrom_mmd_parse` poses the skeleton from the first animation's initial frame (per node: optional scale, XYZ Euler rotation in 1/4096 turns, translation) and merges the parts into one object; this is what `.obj` export uses. `psxrom_mmd_load` instead keeps the bones and runs each animation's keyframe/loop script into per-keyframe poses (`psxrom_mmd_anim`, 20 samples per second of game time).

`mmd`/`mmd-all` with a `.glb`/`.gltf` extension export a node per bone with a PNG-textured material per palette and one glTF animation per MMD animation (`anim<N>`, with `frames`, `loopStart` and `loopEnd` in `extras`). Channels that never leave the rest pose are omitted. Not exported: texture animations (blinking) and sounds. Format knowledge is from SydMontague's [DW1ModelConverter](https://github.com/Operation-Decoded/DW1ModelConverter).
