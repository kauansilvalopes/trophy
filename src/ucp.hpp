// Trophy Manager - the trophy catalog container "TROPHY.UCP". SPDX-License-Identifier: GPL-3.0-or-later
//
// /user/trophy2/nobackup/conf/<NPWR>/TROPHY.UCP holds, for one game: icon0_<lang>.png, trop0000.png.. (one PNG per
// trophy), tropconf.json and tropmeta_<lang>.json (names and details, 14 languages). Layout (big endian):
//   0x00  magic b228c60a   0x10  file count   0x60 + 64 * k  entry k: name[32], u64 offset, u64 size, 16 bytes pad
// The file is tens of megabytes (the pictures), so only the index and the pieces needed are read.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace trophy::ucp
{
constexpr std::size_t entries_at = 0x60;
constexpr std::size_t entry_size = 64;
constexpr std::uint32_t magic = 0xb228c60au;
constexpr std::uint32_t max_entries = 512;

struct Entry
{
    std::string name;
    std::uint64_t offset = 0, size = 0;
};

inline std::uint32_t be32(const std::uint8_t *p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
inline std::uint64_t be64(const std::uint8_t *p) noexcept
{
    return (static_cast<std::uint64_t>(be32(p)) << 32) | be32(p + 4);
}

// From the first 0x20 bytes: how many bytes of the file hold the whole index (0 when it is not a catalog).
inline std::size_t index_bytes(const std::uint8_t *head, std::size_t n) noexcept
{
    if (n < 0x20 || be32(head) != magic)
        return 0;
    const std::uint32_t count = be32(head + 0x10);
    if (count == 0 || count > max_entries)
        return 0;
    return entries_at + static_cast<std::size_t>(count) * entry_size;
}

inline bool parse_index(const std::uint8_t *d, std::size_t n, std::vector<Entry> &out)
{
    out.clear();
    const std::size_t need = index_bytes(d, n);
    if (!need || n < need)
        return false;
    const std::uint32_t count = be32(d + 0x10);
    for (std::uint32_t k = 0; k < count; ++k)
    {
        const std::uint8_t *e = d + entries_at + static_cast<std::size_t>(k) * entry_size;
        Entry x;
        std::size_t len = 0;
        while (len < 32 && e[len])
            ++len;
        x.name.assign(reinterpret_cast<const char *>(e), len);
        x.offset = be64(e + 32);
        x.size = be64(e + 40);
        out.push_back(x);
    }
    return true;
}

inline const Entry *find(const std::vector<Entry> &index, const std::string &name)
{
    for (const Entry &e : index)
        if (e.name == name)
            return &e;
    return nullptr;
}

// The metadata file for the first language of the list that exists, e.g. {"pt-BR", "en-US"}.
inline const Entry *find_meta(const std::vector<Entry> &index, const std::vector<std::string> &languages)
{
    for (const std::string &lang : languages)
        if (const Entry *e = find(index, "tropmeta_" + lang + ".json"))
            return e;
    return nullptr;
}
} // namespace trophy::ucp
