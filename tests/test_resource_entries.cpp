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
    std::vector<uint8_t> image(52 * sector_size);
    const size_t pvd = 16 * sector_size;
    std::memcpy(image.data() + pvd, "\1CD001", 6);
    write_u32(image, pvd + 158, 20);
    write_u32(image, pvd + 166, sector_size);
    if (!empty)
    {
        size_t offset = 20 * sector_size;
        offset = add_record(image, offset, invalid_name ? "BAD/NAME;1" : "ROOT.TXT;1", 22, false);
        offset = add_record(image, offset, "SUBDIR", broken ? 60 : 21, true);
        offset = add_record(image, offset, "EMPTY", 26, true);
        add_record(image, offset, "OTHER", 27, true);
        offset = 21 * sector_size;
        offset = add_record(image, offset, "NESTED.TIM;1", 23, false);
        add_record(image, offset, "INNER", 24, true);
        add_record(image, 24 * sector_size, "NESTED.TIM;1", 25, false);
        add_record(image, 27 * sector_size, "NESTED.TIM;1", 25, false);

        const size_t tim = 23 * sector_size;
        write_u32(image, tim, 0x10);
        write_u32(image, tim + 4, 8);
        write_u32(image, tim + 8, 76);
        image[tim + 12] = 3;
        image[tim + 14] = 4;
        image[tim + 16] = 16;
        image[tim + 18] = 2;
        write_u32(image, tim + 84, 16);
        image[tim + 88] = 5;
        image[tim + 90] = 6;
        image[tim + 92] = 2;
        image[tim + 94] = 1;

        const size_t direct_tim = 22 * sector_size;
        write_u32(image, direct_tim, 0x10);
        write_u32(image, direct_tim + 4, 2);
        write_u32(image, direct_tim + 8, 14);
        image[direct_tim + 16] = 1;
        image[direct_tim + 18] = 1;

        const size_t tmd = 25 * sector_size;
        write_u32(image, tmd, 0x41);
        write_u32(image, tmd + 8, 2);
        for (size_t i = 0; i < 2; ++i)
        {
            const size_t object = tmd + 12 + i * 28;
            write_u32(image, object, 56);
            write_u32(image, object + 4, 3);
            write_u32(image, object + 16, 80);
            write_u32(image, object + 20, 1);
        }
        image[tmd + 93] = 3;
        image[tmd + 94] = 1;
        image[tmd + 95] = 0x20;
        image[tmd + 102] = 1;
        image[tmd + 104] = 2;

        const size_t mmd = 28 * sector_size;
        write_u32(image, mmd, 8);
        write_u32(image, mmd + 4, 116);
        std::memcpy(image.data() + mmd + 8, image.data() + tmd, 108);

        const size_t map = 29 * sector_size;
        write_u32(image, map, 20);
        write_u32(image, map + 4, 196);
        write_u32(image, map + 8, 218);
        write_u32(image, map + 12, 278);
        write_u32(image, map + 16, 400);
        write_u32(image, map + 20, 10);
        write_u32(image, map + 24, UINT32_MAX);
        write_u32(image, map + 32, 23);
        write_u32(image, map + 128, 512);
        write_u32(image, map + 164, 3);
        write_u32(image, map + 168, 2);
        write_u32(image, map + 176, UINT32_MAX);
        write_u32(image, map + 188, UINT32_MAX);
        std::memcpy(image.data() + map + 196, image.data() + direct_tim, 22);
        image[map + 218] = 1;
        image[map + 238] = 1;
        image[map + 398] = 7;

        const size_t tfs = 35 * sector_size;
        image[tfs] = 6;
        image[tfs + 2] = 5;
        write_u32(image, tfs + 4, 2);
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

            const ResourceDetails tim = load_resource_details(
                disc.get(), {"image.TiM", "/image.TiM", 100, 23, false, {}});
            CHECK(tim.type == ResourceDetailType::Tim && tim.error == PSXROM_OK);
            CHECK(tim.width == 8 && tim.height == 1 && tim.bpp == 4);
            CHECK(tim.img_x == 5 && tim.img_y == 6);
            CHECK(tim.has_clut && tim.clut_x == 3 && tim.clut_y == 4);
            CHECK(tim.clut_w == 16 && tim.num_cluts == 2);

            const ResourceDetails direct = load_resource_details(
                disc.get(), {"image.tim", "/image.tim", 22, 22, false, {}});
            CHECK(direct.type == ResourceDetailType::Tim && direct.bpp == 16);
            CHECK(direct.width == 1 && direct.height == 1 && !direct.has_clut);
            CHECK(load_resource_details(disc.get(), entries[0]).type == ResourceDetailType::None);
            CHECK(load_resource_details(disc.get(), {"folder.tim", "/folder.tim", 0, 0, true, {}}).type ==
                  ResourceDetailType::None);

            const ResourceDetails invalid = load_resource_details(disc.get(), entries[1].children[1].children[0]);
            CHECK(invalid.type == ResourceDetailType::Error && invalid.error == PSXROM_ERR_FORMAT);
            const ResourceDetails unreadable = load_resource_details(
                disc.get(), {"bad.tim", "/bad.tim", 100, 60, false, {}});
            CHECK(unreadable.type == ResourceDetailType::Error && unreadable.error == PSXROM_ERR_RANGE);
            const ResourceDetails oversized = load_resource_details(
                disc.get(), {"big.tim", "/big.tim", UINT32_MAX, 23, false, {}});
            CHECK(oversized.type == ResourceDetailType::Error && oversized.error == PSXROM_ERR_RANGE);

            const ResourceDetails tmd = load_resource_details(
                disc.get(), {"model.TmD", "/model.TmD", 108, 25, false, {}});
            CHECK(tmd.type == ResourceDetailType::Tmd && tmd.error == PSXROM_OK);
            CHECK(tmd.object_count == 2 && tmd.triangle_count == 2);
            CHECK(load_resource_details(disc.get(), {"folder.tmd", "/folder.tmd", 0, 0, true, {}}).type ==
                  ResourceDetailType::None);
            const ResourceDetails invalid_tmd = load_resource_details(
                disc.get(), {"bad.tmd", "/bad.tmd", 100, 23, false, {}});
            CHECK(invalid_tmd.type == ResourceDetailType::Error && invalid_tmd.error == PSXROM_ERR_FORMAT);
            const ResourceDetails unreadable_tmd = load_resource_details(
                disc.get(), {"bad.tmd", "/bad.tmd", 100, 60, false, {}});
            CHECK(unreadable_tmd.type == ResourceDetailType::Error && unreadable_tmd.error == PSXROM_ERR_RANGE);

            const ResourceDetails mmd = load_resource_details(
                disc.get(), {"model.MmD", "/model.MmD", 116, 28, false, {}});
            CHECK(mmd.type == ResourceDetailType::Mmd && mmd.error == PSXROM_OK);
            CHECK(mmd.object_count == 2 && mmd.triangle_count == 2);
            CHECK(load_resource_details(disc.get(), {"folder.mmd", "/folder.mmd", 0, 0, true, {}}).type ==
                  ResourceDetailType::None);
            const ResourceDetails invalid_mmd = load_resource_details(
                disc.get(), {"bad.mmd", "/bad.mmd", 100, 23, false, {}});
            CHECK(invalid_mmd.type == ResourceDetailType::Error && invalid_mmd.error == PSXROM_ERR_FORMAT);
            const ResourceDetails unreadable_mmd = load_resource_details(
                disc.get(), {"bad.mmd", "/bad.mmd", 100, 60, false, {}});
            CHECK(unreadable_mmd.type == ResourceDetailType::Error && unreadable_mmd.error == PSXROM_ERR_RANGE);

            const ResourceDetails map = load_resource_details(
                disc.get(), {"area.MaP", "/area.MaP", 10400, 29, false, {}});
            CHECK(map.type == ResourceDetailType::Map && map.error == PSXROM_OK);
            CHECK(map.map_tiles_w == 3 && map.map_tiles_h == 2 && map.map_used_tiles == 4);
            CHECK(map.map_camera_origin[0] == 10 && map.map_camera_origin[1] == -1);
            CHECK(map.map_camera_target[0] == 23 && map.map_view_distance == 512);
            CHECK(map.map_image_count == 1 && map.map_object_count == 1 && map.map_instance_count == 1);
            CHECK(map.map_digimon_count == 7);
            CHECK(load_resource_details(disc.get(), {"folder.map", "/folder.map", 0, 0, true, {}}).type ==
                  ResourceDetailType::None);
            const ResourceDetails invalid_map = load_resource_details(
                disc.get(), {"bad.map", "/bad.map", 100, 23, false, {}});
            CHECK(invalid_map.type == ResourceDetailType::Error && invalid_map.error == PSXROM_ERR_FORMAT);
            const ResourceDetails unreadable_map = load_resource_details(
                disc.get(), {"bad.map", "/bad.map", 100, 60, false, {}});
            CHECK(unreadable_map.type == ResourceDetailType::Error && unreadable_map.error == PSXROM_ERR_RANGE);

            const ResourceDetails tfs = load_resource_details(
                disc.get(), {"area.TfS", "/area.TfS", 8 + 2 * 512 + 2 * (4 + 128 * 128), 35, false, {}});
            CHECK(tfs.type == ResourceDetailType::Tfs && tfs.error == PSXROM_OK);
            CHECK(tfs.tfs_width == 6 && tfs.tfs_height == 5);
            CHECK(tfs.tfs_tile_count == 2 && tfs.tfs_palette_count == 2);
            CHECK(load_resource_details(disc.get(), {"folder.tfs", "/folder.tfs", 0, 0, true, {}}).type ==
                  ResourceDetailType::None);
            const ResourceDetails invalid_tfs = load_resource_details(
                disc.get(), {"bad.tfs", "/bad.tfs", 8, 35, false, {}});
            CHECK(invalid_tfs.type == ResourceDetailType::Error && invalid_tfs.error == PSXROM_ERR_FORMAT);
            const ResourceDetails unreadable_tfs = load_resource_details(
                disc.get(), {"bad.tfs", "/bad.tfs", 100, 60, false, {}});
            CHECK(unreadable_tfs.type == ResourceDetailType::Error && unreadable_tfs.error == PSXROM_ERR_RANGE);
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
