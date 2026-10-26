// Trophy Manager - PS5 trophy progress file (trophy2 "TRPTITLE.DAT", magic "T2PD"). SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout (all integers big endian):
//   0x00..0x3f   header
//   0x40..       blocks of 0x400 data bytes, each followed by the SHA-256 of those bytes (stride 0x420). A block
//                that is entirely zero keeps a zero hash. There is no key.
// Records are contiguous in the *stream* (the data without the hashes), so a field can straddle a hash on disk.
// Everything here edits the stream and rebuilds the hashes at the end. Times are microseconds since 0001-01-01.
//
// What earning a trophy changes (derived from a real before/after pair, reproduced byte for byte):
//   - the trophy's 0x800/0x50 state record: flags 0x11, 0x2000, the time twice, the 16 byte trailer
//   - the earned bitmap (16 bytes, little endian bit = trophy id), kept in three places
//   - the time of the latest earned trophy, kept in three places
//   - the header record 0x2/0x60: latest event time, pending time cleared
//   - two revision counters the console bumps on every write
#pragma once

#include "sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace trophy::t2pd
{
constexpr std::size_t head_size = 0x40;
constexpr std::size_t chunk_size = 0x400;
constexpr std::size_t stride = 0x420;
constexpr std::size_t records_from = 0x1c0; // the section directory ends before this
constexpr std::size_t mask_width = 16;      // 128 trophies at most

inline std::uint32_t be32(const std::uint8_t *p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
inline std::uint64_t be64(const std::uint8_t *p) noexcept
{
    return (static_cast<std::uint64_t>(be32(p)) << 32) | be32(p + 4);
}
inline void put32(std::uint8_t *p, std::uint32_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}
inline void put64(std::uint8_t *p, std::uint64_t v) noexcept
{
    put32(p, static_cast<std::uint32_t>(v >> 32));
    put32(p + 4, static_cast<std::uint32_t>(v));
}

inline std::uint64_t ticks_from_unix(std::int64_t unix_seconds) noexcept
{
    return static_cast<std::uint64_t>(unix_seconds + 62135596800ll) * 1000000ull;
}
inline std::int64_t unix_from_ticks(std::uint64_t ticks) noexcept
{
    return static_cast<std::int64_t>(ticks / 1000000ull) - 62135596800ll;
}

struct Trophy
{
    int id = 0;
    int grade = 0; // 1 platinum, 2 gold, 3 silver, 4 bronze
    bool earned = false;
    std::uint64_t when = 0;
};

// The data without the hashes. False when the file is not a whole number of blocks.
inline bool to_stream(const std::uint8_t *d, std::size_t n, std::vector<std::uint8_t> &s)
{
    s.clear();
    if (n < head_size || (n - head_size) % stride != 0 || std::memcmp(d, "T2PD", 4) != 0)
        return false;
    s.assign(d, d + head_size);
    for (std::size_t o = head_size; o + stride <= n; o += stride)
        s.insert(s.end(), d + o, d + o + chunk_size);
    return true;
}

inline std::vector<std::uint8_t> from_stream(const std::vector<std::uint8_t> &s)
{
    std::vector<std::uint8_t> out(s.begin(), s.begin() + head_size);
    for (std::size_t o = head_size; o + chunk_size <= s.size(); o += chunk_size)
    {
        out.insert(out.end(), s.begin() + o, s.begin() + o + chunk_size);
        bool any = false;
        for (std::size_t i = 0; i < chunk_size && !any; ++i)
            any = s[o + i] != 0;
        if (any)
        {
            const auto h = sha256(s.data() + o, chunk_size);
            out.insert(out.end(), h.begin(), h.end());
        }
        else
            out.insert(out.end(), 32, 0);
    }
    return out;
}

// True when every stored hash matches its block.
inline bool verify(const std::uint8_t *d, std::size_t n)
{
    if (n < head_size || (n - head_size) % stride != 0)
        return false;
    for (std::size_t o = head_size; o + stride <= n; o += stride)
    {
        bool any = false;
        for (std::size_t i = 0; i < chunk_size && !any; ++i)
            any = d[o + i] != 0;
        std::uint8_t want[32] = {};
        if (any)
        {
            const auto h = sha256(d + o, chunk_size);
            std::memcpy(want, h.data(), 32);
        }
        if (std::memcmp(want, d + o + chunk_size, 32) != 0)
            return false;
    }
    return true;
}

// Offsets of the records with this type and size, scanning the stream in 16 byte steps.
inline std::vector<std::size_t> find_records(const std::vector<std::uint8_t> &s, std::uint32_t kind,
                                             std::uint32_t size)
{
    std::vector<std::size_t> out;
    for (std::size_t o = records_from; o + 0x10 <= s.size(); o += 16)
        if (be32(&s[o]) == kind && be32(&s[o + 4]) == size)
            out.push_back(o);
    return out;
}

inline bool parse(const std::vector<std::uint8_t> &s, std::vector<Trophy> &out)
{
    out.clear();
    unsigned char grade[1024] = {};
    for (const std::size_t o : find_records(s, 0x500, 0xc0))
    {
        const std::uint32_t id = be32(&s[o + 0x10]), g = be32(&s[o + 0x14]);
        if (id < 1024 && g >= 1 && g <= 4 && o + 0xc0 <= s.size())
            grade[id] = static_cast<unsigned char>(g);
    }
    bool seen[1024] = {};
    for (const std::size_t o : find_records(s, 0x800, 0x50))
    {
        const std::uint32_t id = be32(&s[o + 0x10]), flags = be32(&s[o + 0x14]);
        if (id >= 1024 || seen[id] || (flags != 0 && flags != 0x10 && flags != 0x11) || o + 0x50 > s.size())
            continue;
        seen[id] = true;
        Trophy t;
        t.id = static_cast<int>(id);
        t.grade = grade[id];
        t.when = be64(&s[o + 0x20]);
        t.earned = flags == 0x11 && t.when != 0;
        out.push_back(t);
    }
    return !out.empty();
}

namespace detail
{
struct Anchors
{
    std::size_t header = 0, r6 = 0, r7 = 0;
    std::vector<std::size_t> states;
};

inline bool anchors(const std::vector<std::uint8_t> &s, Anchors &a)
{
    const auto h = find_records(s, 0x2, 0x60);
    const auto r6 = find_records(s, 0x600, 0x1f0);
    const auto r7 = find_records(s, 0x700, 0xb0);
    if (h.empty() || r6.empty() || r7.empty())
        return false;
    a.header = h.front();
    a.r6 = r6.front();
    a.r7 = r7.front();
    a.states = find_records(s, 0x800, 0x50);
    return a.r7 + 0x50 <= s.size() && a.r6 + 0x190 <= s.size() && a.header + 0x30 <= s.size();
}

inline bool state_of(const std::vector<std::uint8_t> &s, const Anchors &a, int id, std::size_t &out)
{
    for (const std::size_t o : a.states)
        if (be32(&s[o + 0x10]) == static_cast<std::uint32_t>(id) && o + 0x50 <= s.size())
        {
            out = o;
            return true;
        }
    return false;
}

inline void set_bit(std::vector<std::uint8_t> &s, std::size_t off, int id, bool on)
{
    std::uint8_t &b = s[off + static_cast<std::size_t>(id) / 8];
    const std::uint8_t m = static_cast<std::uint8_t>(1u << (id % 8));
    b = on ? static_cast<std::uint8_t>(b | m) : static_cast<std::uint8_t>(b & ~m);
}

inline void add_counters(std::vector<std::uint8_t> &s, const Anchors &a)
{
    put64(&s[a.header + 0x18], be64(&s[a.header + 0x18]) + 2);
    put64(&s[a.r6 + 0x10], be64(&s[a.r6 + 0x10]) + 1);
}
} // namespace detail

// Marks the trophy as earned at `when` (microseconds since 0001-01-01), the way the console records it.
inline bool unlock(std::vector<std::uint8_t> &s, int id, std::uint64_t when)
{
    detail::Anchors a;
    std::size_t o = 0;
    if (id < 0 || id >= static_cast<int>(mask_width * 8) || !detail::anchors(s, a) || !detail::state_of(s, a, id, o))
        return false;
    if (be32(&s[o + 0x14]) == 0x11)
        return false; // already earned
    static const std::uint8_t trailer[16] = {0x55, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 1};
    put32(&s[o + 0x14], 0x11);
    put32(&s[o + 0x18], 0x2000);
    put64(&s[o + 0x20], when);
    put64(&s[o + 0x30], when);
    std::memcpy(&s[o + 0x40], trailer, sizeof trailer);
    for (const std::size_t at : {a.r6 + 0x80, a.r6 + 0x180, a.r7 + 0x40})
        detail::set_bit(s, at, id, true);
    const std::uint64_t last = be64(&s[a.r6 + 0x40]) > when ? be64(&s[a.r6 + 0x40]) : when;
    for (const std::size_t at : {a.r6 + 0x40, a.r6 + 0x70, a.r7 + 0x20})
        put64(&s[at], last);
    if (when > be64(&s[a.header + 0x20]))
    {
        put64(&s[a.header + 0x20], when);
        put64(&s[a.header + 0x28], 0);
    }
    detail::add_counters(s, a);
    return true;
}

// Marks the trophy as not earned again. The latest-earned time falls back to the newest remaining one.
inline bool lock(std::vector<std::uint8_t> &s, int id)
{
    detail::Anchors a;
    std::size_t o = 0;
    if (id < 0 || id >= static_cast<int>(mask_width * 8) || !detail::anchors(s, a) || !detail::state_of(s, a, id, o))
        return false;
    if (be32(&s[o + 0x14]) != 0x11)
        return false; // not earned
    std::memset(&s[o + 0x14], 0, 0x50 - 0x14);
    for (const std::size_t at : {a.r6 + 0x80, a.r6 + 0x180, a.r7 + 0x40})
        detail::set_bit(s, at, id, false);
    std::uint64_t last = 0;
    for (const std::size_t at : a.states)
        if (at + 0x50 <= s.size() && be32(&s[at + 0x14]) == 0x11 && be64(&s[at + 0x20]) > last)
            last = be64(&s[at + 0x20]);
    for (const std::size_t at : {a.r6 + 0x40, a.r6 + 0x70, a.r7 + 0x20})
        put64(&s[at], last);
    detail::add_counters(s, a);
    return true;
}
} // namespace trophy::t2pd
