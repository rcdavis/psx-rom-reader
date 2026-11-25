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

bool write_image(const char* path, bool empty, bool broken, bool invalid_name)
{
    std::vector<uint8_t> image(28 * sector_size);
    const size_t pvd = 16 * sector_size;
    std::memcpy(image.data() + pvd, "\1CD001", 6);
    write_u32(image, pvd + 158, 20);
    write_u32(image, pvd + 166, sector_size);
    if (!empty)
    {
        size_t offset = 20 * sector_size;
        offset = add_record(image, offset, invalid_name ? "BAD/NAME;1" : "ROOT.TXT;1", 22, false);
        offset = add_record(image, offset, "SUBDIR", broken ? 30 : 21, true);
        offset = add_record(image, offset, "EMPTY", 26, true);
        add_record(image, offset, "OTHER", 27, true);
        offset = 21 * sector_size;
        offset = add_record(image, offset, "NESTED.TIM;1", 23, false);
        add_record(image, offset, "INNER", 24, true);
        add_record(image, 24 * sector_size, "NESTED.TIM;1", 25, false);
        add_record(image, 27 * sector_size, "NESTED.TIM;1", 25, false);
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
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        CHECK(write_image(argv[1], scenario == 1, scenario == 2, scenario == 3));
        psxrom_disc* opened = nullptr;
        CHECK(psxrom_disc_open(argv[1], &opened) == PSXROM_OK);
        const std::unique_ptr<psxrom_disc, decltype(&psxrom_disc_close)> disc(opened, psxrom_disc_close);
        std::vector<ResourceEntry> entries = {{"stale", "/stale", 0, 0, false, {}}};
        const psxrom_result result = load_resource_entries(disc.get(), entries);
        if (scenario == 0)
        {
            CHECK(result == PSXROM_OK);
            CHECK(entries.size() == 4);
            CHECK(entries[0].name == "ROOT.TXT" && !entries[0].is_directory && entries[0].children.empty());
            CHECK(entries[0].path == "/ROOT.TXT" && entries[0].size == 1 && entries[0].lba == 22);
            CHECK(entries[1].name == "SUBDIR" && entries[1].is_directory && entries[1].children.size() == 2);
            CHECK(entries[1].children[0].name == "NESTED.TIM" && !entries[1].children[0].is_directory);
            CHECK(entries[1].children[1].name == "INNER" && entries[1].children[1].is_directory);
            CHECK(entries[1].children[1].children.size() == 1);
            CHECK(entries[1].children[1].children[0].name == "NESTED.TIM");
            CHECK(entries[1].children[1].children[0].path == "/SUBDIR/INNER/NESTED.TIM");
            CHECK(entries[1].children[1].children[0].size == 1 && entries[1].children[1].children[0].lba == 25);
            CHECK(entries[2].name == "EMPTY" && entries[2].is_directory && entries[2].children.empty());
            CHECK(entries[3].name == "OTHER" && entries[3].is_directory && entries[3].children.size() == 1);
            CHECK(entries[3].children[0].name == "NESTED.TIM");
            CHECK(entries[3].children[0].path == "/OTHER/NESTED.TIM");
        }
        else
        {
            CHECK(scenario == 1 ? result == PSXROM_OK : result != PSXROM_OK);
            if (scenario == 3)
                CHECK(result == PSXROM_ERR_FORMAT);
            CHECK(entries.empty());
        }
    }
    CHECK(std::remove(argv[1]) == 0);
    return 0;
}
