# psx-rom-reader
Reads assets in a PSX rom. A dependency-free C99 library (`psxrom`) plus a command line viewer (`psxview`).

Supports `.iso` (2048) and raw `.bin`/`.img` (2352 / 2336 byte sectors) images, ISO9660 browsing/extraction, and TIM texture decoding.

## Use via CMake FetchContent
```cmake
include(FetchContent)
FetchContent_Declare(psxrom
    GIT_REPOSITORY https://github.com/rcdavis/psx-rom-reader.git
    GIT_TAG main)
FetchContent_MakeAvailable(psxrom)
target_link_libraries(my_app PRIVATE psxrom::psxrom)
```
The viewer is only built when this is the top-level project (`-DPSXROM_BUILD_VIEWER=ON` to force).

```c
#include <psxrom/psxrom.h>
psxrom_disc *d; psxrom_disc_open("game.bin", &d);
psxrom_entry e; psxrom_find(d, "/SYSTEM.CNF", &e);
uint8_t *data; size_t n; psxrom_read_file(d, &e, &data, &n);
```

## Viewer
```
cmake -B build && cmake --build build
build/psxview game.bin info | ls | tims
build/psxview game.bin hex /SYSTEM.CNF
build/psxview game.bin extract /DIR/FILE.TIM out.tim
build/psxview game.bin extract-all out/
build/psxview game.bin tim /DIR/ICON.TIM icon.bmp [palette]
build/psxview game.bin tim-all textures/
```
