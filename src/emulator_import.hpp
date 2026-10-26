#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <initializer_list>

namespace estante
{
inline bool join_import_path(const char *base, const char *leaf, char *out, std::size_t cap)
{
    if (!base || !leaf || !out || cap == 0)
        return false;
    const int n = std::snprintf(out, cap, "%s/%s", base, leaf);
    return n >= 0 && static_cast<std::size_t>(n) < cap;
}

// SwanStationPS5 names a cover after the game id (src/library.c, finish_game): without a serial the id
// is the title with every BYTE that is not an ASCII letter or digit replaced by '_' (isalnum on an
// unsigned char), cut to 63 characters (char id[64]). A multi-byte UTF-8 letter therefore becomes one
// underscore per byte, and covers/boxart/<id>.png is what the emulator looks for. (When it finds a
// serial in the folder name or a serial.txt, the id is the serial instead; that case is not
// reproduced here.)
inline bool swan_cover_filename(const char *title, char *out, std::size_t cap)
{
    constexpr std::size_t id_max = 63;
    if (!title || !out || cap < id_max + 5)
        return false;
    // Mirror clean_title() in SwanStationPS5/src/library.c. The game title is
    // capped at 95 bytes after removing bracket tags, selected disc tags,
    // replacing underscores with spaces and collapsing whitespace. The ID
    // sanitizer below then processes bytes, not Unicode code points.
    char raw[256]{};
    std::size_t raw_len = std::strlen(title);
    if (raw_len >= sizeof raw)
        raw_len = sizeof raw - 1;
    std::memcpy(raw, title, raw_len);
    std::size_t dot = raw_len;
    while (dot > 0 && raw[dot - 1] != '.')
        --dot;
    if (dot > 0)
    {
        static constexpr const char *extensions[] = {"m3u", "pbp", "chd", "cue", "ccd",
                                                     "iso", "img", "mdf", "bin"};
        const std::size_t ext_len = raw_len - dot;
        for (const char *extension : extensions)
        {
            if (std::strlen(extension) != ext_len)
                continue;
            bool equal = true;
            for (std::size_t i = 0; i < ext_len; ++i)
            {
                unsigned char c = static_cast<unsigned char>(raw[dot + i]);
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<unsigned char>(c - 'A' + 'a');
                if (c != static_cast<unsigned char>(extension[i]))
                    equal = false;
            }
            if (equal)
            {
                raw_len = dot - 1;
                raw[raw_len] = 0;
                break;
            }
        }
    }
    char clean[96]{};
    std::size_t clean_len = 0;
    bool pending_space = true;
    for (std::size_t i = 0; i < raw_len && clean_len < sizeof clean - 1;)
    {
        if (raw[i] == '[')
        {
            std::size_t end = i + 1;
            while (end < raw_len && raw[end] != ']')
                ++end;
            if (end < raw_len)
            {
                i = end + 1;
                continue;
            }
        }
        const std::size_t remain = raw_len - i;
        if (raw[i] == '(' &&
            ((remain >= 5 && (!std::memcmp(raw + i, "(Disc", 5) ||
                              !std::memcmp(raw + i, "(disc", 5))) ||
             (remain >= 3 && !std::memcmp(raw + i, "(CD", 3)) ||
             (remain >= 7 && !std::memcmp(raw + i, "(SwanStationPS5)", 7))))
        {
            std::size_t end = i + 1;
            while (end < raw_len && raw[end] != ')')
                ++end;
            if (end < raw_len)
            {
                i = end + 1;
                continue;
            }
        }
        const unsigned char c = static_cast<unsigned char>(raw[i++]);
        const unsigned char normalized = c == '_' ? ' ' : c;
        if (std::isspace(normalized))
        {
            if (!pending_space)
                clean[clean_len++] = ' ';
            pending_space = true;
        }
        else
        {
            clean[clean_len++] = static_cast<char>(normalized);
            pending_space = false;
        }
    }
    if (clean_len && clean[clean_len - 1] == ' ')
        --clean_len;
    clean[clean_len] = 0;

    std::size_t used = 0;
    for (const auto *p = reinterpret_cast<const unsigned char *>(clean); *p && used < id_max; ++p)
        out[used++] = (*p < 0x80 && std::isalnum(*p) != 0) ? static_cast<char>(*p) : '_';
    std::memcpy(out + used, ".png", 5);
    return true;
}

// A readable title from a ROM file name: no extension, no "(USA)" / "[!]" groups, no repeated spaces, and
// the Redump habit of ", The" at the end moved to the front ("Simpsons, The - Hit & Run" becomes
// "The Simpsons - Hit & Run"). Returns false when nothing readable is left.
inline bool clean_rom_title(const char *file, char *out, std::size_t cap)
{
    if (!file || !out || cap < 2)
        return false;
    char stem[256];
    std::size_t length = std::strlen(file);
    if (length >= sizeof stem)
        return false;
    std::memcpy(stem, file, length + 1);
    const char *dot = std::strrchr(stem, '.');
    if (dot && dot != stem && std::strlen(dot) <= 6 && !std::strchr(dot, ' '))
        length = static_cast<std::size_t>(dot - stem);
    char text[256];
    std::size_t used = 0;
    int depth = 0;
    bool space = false;
    for (std::size_t i = 0; i < length; ++i)
    {
        const char c = stem[i];
        if (c == '(' || c == '[')
        {
            ++depth;
            continue;
        }
        if ((c == ')' || c == ']') && depth > 0)
        {
            --depth;
            continue;
        }
        if (depth > 0)
            continue;
        if (c == ' ' || c == '	')
        {
            space = used > 0;
            continue;
        }
        if (space && used + 1 < sizeof text)
            text[used++] = ' ';
        space = false;
        if (used + 1 < sizeof text)
            text[used++] = c;
    }
    text[used] = 0;
    while (used > 0 && (text[used - 1] == '-' || text[used - 1] == ' '))
        text[--used] = 0;
    if (used == 0)
        return false;
    // ", The" / ", A" / ", An" before the end or before " - ".
    for (const char *article : {"The", "A", "An"})
    {
        char marker[8];
        std::snprintf(marker, sizeof marker, ", %s", article);
        const char *at = std::strstr(text, marker);
        const std::size_t width = std::strlen(marker);
        if (at && (at[width] == 0 || at[width] == ' ') && at != text)
        {
            char moved[256];
            const int n = std::snprintf(moved, sizeof moved, "%s %.*s%s", article,
                                        static_cast<int>(at - text), text, at + width);
            if (n > 0 && static_cast<std::size_t>(n) < sizeof moved)
                std::memcpy(text, moved, static_cast<std::size_t>(n) + 1);
            break;
        }
    }
    if (std::strlen(text) >= cap)
        return false;
    std::memcpy(out, text, std::strlen(text) + 1);
    return true;
}

// A title that tells the player nothing (only digits/hex, or shorter than three characters).
inline bool meaningless_title(const char *title)
{
    if (!title)
        return true;
    std::size_t length = 0, hex = 0;
    for (const char *p = title; *p; ++p, ++length)
        hex += std::isxdigit(static_cast<unsigned char>(*p)) != 0;
    return length < 3 || (length >= 8 && hex == length);
}

// Boot serial of a PlayStation disc image ("SLUS-00684"), read from SYSTEM.CNF (BOOT = cdrom:\SLUS_006.84;1).
// `data` holds only the start of the image: raw 2352-byte sectors (sync pattern, data after 16 or 24 bytes) or
// plain 2048-byte sectors. Returns false when SYSTEM.CNF lies beyond what was given.
inline bool ps1_boot_serial(const unsigned char *data, std::size_t size, char *out, std::size_t cap)
{
    if (!data || !out || cap < 12)
        return false;
    static const unsigned char sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
    std::size_t sector = 2048, offset = 0;
    if (size >= 16 && std::memcmp(data, sync, 12) == 0)
    {
        sector = 2352;
        offset = data[15] == 2 ? 24 : 16;
    }
    auto le32 = [](const unsigned char *p)
    {
        return static_cast<std::size_t>(p[0]) | (static_cast<std::size_t>(p[1]) << 8) |
               (static_cast<std::size_t>(p[2]) << 16) | (static_cast<std::size_t>(p[3]) << 24);
    };
    auto at = [&](std::size_t lba) -> const unsigned char *
    {
        const std::size_t start = lba * sector + offset;
        return lba < (std::size_t{1} << 24) && start + 2048 <= size ? data + start : nullptr;
    };
    const unsigned char *pvd = at(16);
    if (!pvd || std::memcmp(pvd + 1, "CD001", 5) != 0)
        return false;
    const std::size_t root_lba = le32(pvd + 158), root_len = le32(pvd + 166);
    if (root_len == 0 || root_len > 8 * 2048)
        return false;
    for (std::size_t block = 0; block * 2048 < root_len; ++block)
    {
        const unsigned char *dir = at(root_lba + block);
        if (!dir)
            return false;
        for (std::size_t pos = 0; pos + 34 < 2048;)
        {
            const std::size_t length = dir[pos];
            if (length == 0)
                break;
            const std::size_t name_len = dir[pos + 32];
            if (pos + 33 + name_len > 2048)
                break;
            if (name_len >= 10 &&
                !strncasecmp(reinterpret_cast<const char *>(dir + pos + 33), "SYSTEM.CNF", 10))
            {
                const unsigned char *text = at(le32(dir + pos + 2));
                if (!text)
                    return false;
                const char *cnf = reinterpret_cast<const char *>(text);
                const char *boot = nullptr;
                for (std::size_t i = 0; i + 6 < 2048; ++i)
                    if (!strncasecmp(cnf + i, "cdrom:", 6))
                    {
                        boot = cnf + i + 6;
                        break;
                    }
                if (!boot)
                    return false;
                if (*boot == '\\' || *boot == '/')
                    ++boot;
                char letters[8]{}, digits[16]{};
                std::size_t l = 0, d = 0;
                for (; l < 4 && std::isalpha(static_cast<unsigned char>(boot[l])); ++l)
                    letters[l] = static_cast<char>(std::toupper(static_cast<unsigned char>(boot[l])));
                if (l != 4)
                    return false;
                const char *rest = boot + l;
                if (*rest == '_' || *rest == '-')
                    ++rest;
                for (; *rest && *rest != ';' && *rest != '\r' && *rest != '\n' && d + 1 < sizeof digits;
                     ++rest)
                    if (std::isdigit(static_cast<unsigned char>(*rest)))
                        digits[d++] = *rest;
                if (d < 5)
                    return false;
                const int n = std::snprintf(out, cap, "%s-%s", letters, digits);
                return n > 0 && static_cast<std::size_t>(n) < cap;
            }
            pos += length;
        }
    }
    return false;
}

inline bool make_swan_paths(const char *games_dir, const char *covers_dir, const char *title,
                            char *game_path, std::size_t game_cap, char *cover_path,
                            std::size_t cover_cap)
{
    char game[200]{}, cover[200]{}, filename[200]{};
    if (!join_import_path(games_dir, title, game, sizeof game) ||
        !swan_cover_filename(title, filename, sizeof filename) ||
        !join_import_path(covers_dir, filename, cover, sizeof cover) || !game_path || !cover_path ||
        std::strlen(game) >= game_cap || std::strlen(cover) >= cover_cap)
        return false;
    std::memcpy(game_path, game, std::strlen(game) + 1);
    std::memcpy(cover_path, cover, std::strlen(cover) + 1);
    return true;
}

inline bool encode_fs_request(const char *request, char *out, std::size_t cap)
{
    if (!request || !out || cap == 0)
        return false;
    const char *value = std::strstr(request, "?path=");
    if (!value)
    {
        const std::size_t n = std::strlen(request);
        if (n >= cap)
            return false;
        std::memcpy(out, request, n + 1);
        return true;
    }
    value += 6;
    const std::size_t prefix = static_cast<std::size_t>(value - request);
    if (prefix >= cap)
        return false;
    std::memcpy(out, request, prefix);
    std::size_t used = prefix;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto *p = reinterpret_cast<const unsigned char *>(value); *p; ++p)
    {
        const unsigned char c = *p;
        const bool escaped = c == '%' && p[1] && p[2] &&
                             ((p[1] >= '0' && p[1] <= '9') || (p[1] >= 'A' && p[1] <= 'F') ||
                              (p[1] >= 'a' && p[1] <= 'f')) &&
                             ((p[2] >= '0' && p[2] <= '9') || (p[2] >= 'A' && p[2] <= 'F') ||
                              (p[2] >= 'a' && p[2] <= 'f'));
        if (escaped)
        {
            if (used + 3 >= cap)
                return false;
            out[used++] = static_cast<char>(p[0]);
            out[used++] = static_cast<char>(p[1]);
            out[used++] = static_cast<char>(p[2]);
            p += 2;
            continue;
        }
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                          c == '~' || c == '/';
        const std::size_t need = safe ? 1 : 3;
        if (used + need >= cap)
            return false;
        if (safe)
            out[used++] = static_cast<char>(c);
        else
        {
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 15];
        }
    }
    out[used] = 0;
    return true;
}

inline bool build_http_request(const char *path, char *out, std::size_t cap)
{
    if (!path || !out || cap == 0)
        return false;
    const int n = std::snprintf(out, cap,
                                "GET %s HTTP/1.0\r\nHost: 127.0.0.1:6969\r\n"
                                "Connection: close\r\n\r\n",
                                path);
    return n >= 0 && static_cast<std::size_t>(n) < cap;
}

// True when two titles differ only in punctuation, spacing or case ("Bowsers Fury" and "Bowser's Fury"): the
// ProsperoEden cover list names a game by its real title, the roms folder by a file-system safe one.
inline bool same_title(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    auto next = [](const char *&p) -> int
    {
        while (*p)
        {
            const unsigned char c = static_cast<unsigned char>(*p++);
            if (std::isalnum(c))
                return std::tolower(c);
            if (c >= 0x80)
                return c;
        }
        return -1;
    };
    for (;;)
    {
        const int x = next(a), y = next(b);
        if (x != y)
            return false;
        if (x < 0)
            return true;
    }
}
inline bool import_slot_available(int count, int capacity, bool &limit_reached)
{
    if (count < capacity)
        return true;
    limit_reached = true;
    return false;
}

inline bool take_limit_warning(bool &limit_reached)
{
    const bool show = limit_reached;
    limit_reached = false;
    return show;
}
} // namespace estante
