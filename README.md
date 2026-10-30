# psx-rom-reader
Reads assets in a PSX rom. The repository is organized as a CMake superproject containing a dependency-free C99 library (`lib/`) and a command line viewer (`viewer/`).

Supports `.iso` (2048) and raw `.bin`/`.img` (2352 / 2336 byte sectors) images, ISO9660 browsing/extraction, and TIM texture decoding.

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
```
