#include "resource_entries.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; \
} } while (0)

namespace
{

constexpr size_t sector_size = 2048;

void write_u32(std::vector<uint8_t>& image, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        image[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

size_t add_record(std::vector<uint8_t>& image, size_t offset, const char* name,
                  uint32_t lba, bool is_dir)
{
    const size_t name_length = std::strlen(name);
    const size_t length = 33 + name_length + (name_length % 2 == 0 ? 1 : 0);
    image[offset] = static_cast<uint8_t>(length);
    write_u32(image, offset + 2, lba);
    write_u32(image, offset + 10, is_dir ? sector_size : 1);
    image[offset + 25] = is_dir ? 2 : 0;
    image[offset + 32] = static_cast<uint8_t>(name_length);
    std::memcpy(image.data() + offset + 33, name, name_length);
    return offset + length;
}

bool write_image(const char* path, bool empty, bool broken)
{
    std::vector<uint8_t> image(24 * sector_size);
    const size_t pvd = 16 * sector_size;
    std::memcpy(image.data() + pvd, "\1CD001", 6);
    write_u32(image, pvd + 158, 20);
    write_u32(image, pvd + 166, sector_size);
    if (!empty)
    {
        size_t offset = 20 * sector_size;
        offset = add_record(image, offset, "ROOT.TXT;1", 22, false);
        add_record(image, offset, "SUBDIR", broken ? 30 : 21, true);
        add_record(image, 21 * sector_size, "NESTED.TIM;1", 23, false);
    }

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    out.close();
    return out.good();
}

}

int main(int argc, char* argv[])
{
    CHECK(argc == 2);
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        CHECK(write_image(argv[1], scenario == 1, scenario == 2));
        psxrom_disc* opened = nullptr;
        CHECK(psxrom_disc_open(argv[1], &opened) == PSXROM_OK);
        const std::unique_ptr<psxrom_disc, decltype(&psxrom_disc_close)> disc(opened, psxrom_disc_close);
        std::vector<std::string> entries = {"stale"};
        const psxrom_result result = load_resource_entries(disc.get(), entries);
        if (scenario == 0)
        {
            CHECK(result == PSXROM_OK);
            CHECK((entries == std::vector<std::string>{"/ROOT.TXT", "/SUBDIR/", "/SUBDIR/NESTED.TIM"}));
        }
        else
        {
            CHECK(scenario == 1 ? result == PSXROM_OK : result != PSXROM_OK);
            CHECK(entries.empty());
        }
    }
    CHECK(std::remove(argv[1]) == 0);
    return 0;
}
