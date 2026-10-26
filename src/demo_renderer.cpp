/*
 * ps5-native-app-boilerplate - CPU VideoOut demonstration implementation.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Provides the bounded drawing surface and PS5 presentation loop used by the
 * editable starter application.
 */

#include "demo_renderer.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceKernelDebugOutText(int channel, const char *text);
    int sceSystemServiceHideSplashScreen();
    int open(const char *path, int flags, ...);
    long read(int descriptor, void *buffer, std::size_t size);
    int close(int descriptor);
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
    bool ps5ObserveOwnedAllocation(const void *address) noexcept;
}

namespace ps5::demo
{
namespace
{
bool frame_drawn = false; // set by any Canvas drawing call; read by the presentation loop
constexpr unsigned frame_width = 1920;
constexpr unsigned frame_height = 1080;
constexpr std::size_t frame_bytes = 0x1000000;
constexpr std::size_t memory_bytes = frame_bytes * 2;
constexpr std::size_t memory_alignment = 0x200000;
constexpr int memory_type_wc_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};

struct Glyph
{
    char character;
    std::array<std::uint8_t, 7> rows;
};

constexpr std::array<Glyph, 49> glyphs{{
    {' ', {0, 0, 0, 0, 0, 0, 0}},        {'.', {0, 0, 0, 0, 0, 12, 12}},
    {':', {0, 12, 12, 0, 12, 12, 0}},    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'%', {24, 25, 2, 4, 8, 19, 3}},     {'/', {1, 1, 2, 4, 8, 16, 16}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 14}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'@', {14, 17, 23, 21, 23, 16, 14}}, {'(', {2, 4, 8, 8, 8, 4, 2}},
    {')', {8, 4, 2, 2, 2, 4, 8}},        {'_', {0, 0, 0, 0, 0, 0, 31}},
    {'!', {4, 4, 4, 4, 4, 0, 4}},        {'?', {14, 17, 1, 2, 4, 0, 4}},
    {',', {0, 0, 0, 0, 12, 4, 8}},
}};

class File final
{
  public:
    explicit File(int descriptor = -1) noexcept : descriptor_{descriptor}
    {
    }

    ~File()
    {
        reset();
    }

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    File(File &&other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)}
    {
    }

    File &operator=(File &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return descriptor_ >= 0;
    }

    [[nodiscard]] int get() const noexcept
    {
        return descriptor_;
    }

  private:
    void reset() noexcept
    {
        if (descriptor_ >= 0)
        {
            (void)close(descriptor_);
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

class LifetimeProbe final
{
  public:
    explicit LifetimeProbe(bool &destroyed) noexcept : destroyed_{&destroyed}
    {
    }

    ~LifetimeProbe()
    {
        *destroyed_ = true;
    }

    LifetimeProbe(const LifetimeProbe &) = delete;
    LifetimeProbe &operator=(const LifetimeProbe &) = delete;

  private:
    bool *destroyed_;
};

NotificationRequest notification{};

void copy_message(std::span<char> destination, std::string_view source) noexcept
{
    if (destination.empty())
        return;

    const std::size_t count =
        source.size() < destination.size() - 1 ? source.size() : destination.size() - 1;
    for (std::size_t index = 0; index < count; ++index)
        destination[index] = source[index];
    destination[count] = '\0';
}

void notify(std::string_view message) noexcept
{
    copy_message(std::span{notification.message}, message);
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

[[nodiscard]] bool verify_unique_ownership() noexcept
{
    bool destroyed = false;
    {
        std::unique_ptr<LifetimeProbe> probe{new (std::nothrow_t{}) LifetimeProbe{destroyed}};
        if (!ps5ObserveOwnedAllocation(probe.get()))
            return false;
    }
    return destroyed;
}

[[noreturn]] void halt(std::string_view message) noexcept
{
    notify(message);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

[[nodiscard]] constexpr std::span<const std::uint8_t, 7> glyph_rows(char character) noexcept
{
    for (const auto &glyph : glyphs)
    {
        if (glyph.character == character)
            return glyph.rows;
    }
    return glyphs.front().rows;
}

// Same swizzle as before, split into a per-column and a per-row part (the XOR terms are separable).
struct OffsetTables
{
    std::array<std::uint32_t, frame_width> column{}; // block column << 16 | x terms
    std::array<std::uint32_t, frame_height> row_term{};
    std::array<std::uint32_t, frame_height> row_base{};
};
[[nodiscard]] constexpr OffsetTables make_offset_tables() noexcept
{
    OffsetTables t{};
    constexpr std::uint32_t blocks_per_row = (frame_width + 127U) >> 7;
    for (unsigned x = 0; x < frame_width; ++x)
        t.column[x] = ((x >> 7) << 16) | (((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                                         ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^
                                         ((x << 9) & 0xa000U));
    for (unsigned y = 0; y < frame_height; ++y)
    {
        t.row_term[y] = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                        ((y << 8) & 0x4000U);
        t.row_base[y] = (y >> 7) * blocks_per_row << 16;
    }
    return t;
}
constexpr OffsetTables offset_tables = make_offset_tables();

[[nodiscard]] inline std::size_t tiled_byte_offset(unsigned x, unsigned y) noexcept
{
    return static_cast<std::size_t>(offset_tables.row_base[y]) +
           (offset_tables.column[x] ^ offset_tables.row_term[y]);
}

void put_pixel_unchecked(std::uint32_t *pixels, unsigned x, unsigned y, Color color) noexcept
{
    auto *bytes = reinterpret_cast<std::uint8_t *>(pixels);
    *reinterpret_cast<std::uint32_t *>(bytes + tiled_byte_offset(x, y)) =
        static_cast<std::uint32_t>(color);
}

void fill_rect(std::uint32_t *pixels, unsigned x, unsigned y, unsigned width, unsigned height,
               Color color) noexcept
{
    if (x >= frame_width || y >= frame_height)
        return;

    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned bottom = height > frame_height - y ? frame_height : y + height;
    for (unsigned row = y; row < bottom; ++row)
    {
        for (unsigned column = x; column < right; ++column)
            put_pixel_unchecked(pixels, column, row, color);
    }
}

void fill_circle(std::uint32_t *pixels, unsigned center_x, unsigned center_y, unsigned radius,
                 Color color) noexcept
{
    const int signed_radius = static_cast<int>(radius);
    for (int y = -signed_radius; y <= signed_radius; ++y)
    {
        for (int x = -signed_radius; x <= signed_radius; ++x)
        {
            if (x * x + y * y <= signed_radius * signed_radius)
            {
                const int pixel_x = static_cast<int>(center_x) + x;
                const int pixel_y = static_cast<int>(center_y) + y;
                if (pixel_x >= 0 && pixel_y >= 0)
                {
                    const auto bounded_x = static_cast<unsigned>(pixel_x);
                    const auto bounded_y = static_cast<unsigned>(pixel_y);
                    if (bounded_x < frame_width && bounded_y < frame_height)
                        put_pixel_unchecked(pixels, bounded_x, bounded_y, color);
                }
            }
        }
    }
}

void fill_triangle(std::uint32_t *pixels, unsigned center_x, unsigned top, unsigned half_width,
                   unsigned height, Color color) noexcept
{
    if (height == 0 || center_x >= frame_width)
        return;

    for (unsigned row = 0; row < height; ++row)
    {
        const unsigned half = row * half_width / height;
        const unsigned left = half > center_x ? 0 : center_x - half;
        const unsigned right = half >= frame_width - center_x ? frame_width : center_x + half + 1;
        fill_rect(pixels, left, top + row, right - left, 1, color);
    }
}

void draw_text(std::uint32_t *pixels, unsigned x, unsigned y, std::string_view value,
               unsigned scale, Color color) noexcept
{
    for (const char character : value)
    {
        const auto rows = glyph_rows(character);
        for (unsigned row = 0; row < rows.size(); ++row)
        {
            for (unsigned column = 0; column < 5; ++column)
            {
                if ((rows[row] & (1U << (4 - column))) != 0)
                    fill_rect(pixels, x + column * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 6 * scale;
        if (x >= frame_width)
            return;
    }
}

void flush_range(void *address, std::size_t length) noexcept
{
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
} // namespace

namespace
{
struct FontGlyph
{
    std::uint32_t code, size, offset;
    std::int32_t left, top;
    std::uint32_t width, height, advance;
};
unsigned char *font_data = nullptr;
std::size_t font_bytes = 0;
unsigned glyph_count = 0;
bool font_tried = false;
void load_font()
{
    if (font_tried)
        return;
    font_tried = true;
#ifdef ESTANTE_HOST_PREVIEW
    const char *path = "assets/fonts/inter.atlas";
#else
    const char *path = "/app0/assets/fonts/inter.atlas";
#endif
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return;
    if (std::fseek(f, 0, SEEK_END))
    {
        std::fclose(f);
        return;
    }
    long n = std::ftell(f);
    std::rewind(f);
    if (n < 8 || n > 4 * 1024 * 1024)
    {
        std::fclose(f);
        return;
    }
    auto *bytes = static_cast<unsigned char *>(std::malloc(n));
    if (!bytes)
    {
        std::fclose(f);
        return;
    }
    bool ok = std::fread(bytes, 1, n, f) == static_cast<std::size_t>(n);
    std::fclose(f);
    std::uint32_t header[2]{};
    std::memcpy(header, bytes, 8);
    if (!ok || header[0] != 0x46545345 || header[1] > 4096 ||
        8 + header[1] * sizeof(FontGlyph) > static_cast<std::size_t>(n))
    {
        std::free(bytes);
        return;
    }
    glyph_count = header[1];
    font_data = bytes;
    font_bytes = n;
}
std::uint32_t next_code(std::string_view s, std::size_t &i)
{
    unsigned c = static_cast<unsigned char>(s[i++]);
    if (c < 128)
        return c;
    int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (!n)
        return '?';
    c &= (1u << (6 - n)) - 1;
    while (n--)
    {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80)
            return '?';
        c = (c << 6) | (static_cast<unsigned char>(s[i++]) & 63);
    }
    return c;
}
const FontGlyph *glyph_for(std::uint32_t code, unsigned size)
{
    auto *glyphs = reinterpret_cast<const FontGlyph *>(font_data + 8);
    for (unsigned i = 0; i < glyph_count; i++)
        if (glyphs[i].code == code && glyphs[i].size == size)
            return glyphs + i;
    for (unsigned i = 0; i < glyph_count; i++)
        if (glyphs[i].code == '?' && glyphs[i].size == size)
            return glyphs + i;
    return nullptr;
}
} // namespace
void Canvas::dim() noexcept
{
    frame_drawn = true;
    for (unsigned y = 0; y < frame_height; y++)
        for (unsigned x = 0; x < frame_width; x++)
        {
            auto *d = reinterpret_cast<std::uint32_t *>(reinterpret_cast<unsigned char *>(pixels_) +
                                                        tiled_byte_offset(x, y));
            *d = 0xff000000u | ((*d & 0x00fefefeu) >> 1);
        }
}
unsigned Canvas::measure(std::string_view value, unsigned size) noexcept
{
    load_font();
    if (!font_data)
        return static_cast<unsigned>(value.size()) * (size >= 44 ? 30U : 18U);
    unsigned width = 0;
    for (std::size_t i = 0; i < value.size();)
    {
        const auto *g = glyph_for(next_code(value, i), size);
        if (g)
            width += g->advance;
    }
    return width;
}

void Canvas::label(unsigned x, unsigned y, std::string_view value, unsigned size, Color color,
                   unsigned max_width) noexcept
{
    frame_drawn = true;
    load_font();
    if (!font_data)
    {
        text(x, y, value, size >= 44 ? 5 : 3, color);
        return;
    }
    const unsigned origin = x;
    const auto fg = static_cast<std::uint32_t>(color);
    for (std::size_t i = 0; i < value.size();)
    {
        auto code = next_code(value, i);
        auto *g = glyph_for(code, size);
        if (!g)
            continue;
        const auto *dots = glyph_for(0x2026, size);
        bool truncated =
            x - origin + g->advance + (i < value.size() && dots ? dots->advance : 0) > max_width;
        if (truncated)
        {
            if (!dots || x - origin + dots->advance > max_width)
                break;
            g = dots;
            i = value.size();
        }
        const std::size_t start = 8 + glyph_count * sizeof(FontGlyph) + g->offset;
        if (g->width > 256 || g->height > 256 || start + g->width * g->height > font_bytes)
            continue;
        for (unsigned yy = 0; yy < g->height; yy++)
            for (unsigned xx = 0; xx < g->width; xx++)
            {
                unsigned a = font_data[start + yy * g->width + xx];
                if (!a)
                    continue;
                int px = static_cast<int>(x) + g->left + xx,
                    py = static_cast<int>(y + size) + g->top + yy;
                if (px < 0 || py < 0 || px >= static_cast<int>(frame_width) ||
                    py >= static_cast<int>(frame_height))
                    continue;
                auto *dest = reinterpret_cast<std::uint32_t *>(
                    reinterpret_cast<unsigned char *>(pixels_) + tiled_byte_offset(px, py));
                unsigned old = *dest, inv = 255 - a,
                         red = ((fg & 255) * a + (old & 255) * inv) / 255,
                         green = (((fg >> 8) & 255) * a + ((old >> 8) & 255) * inv) / 255,
                         blue = (((fg >> 16) & 255) * a + ((old >> 16) & 255) * inv) / 255;
                *dest = 0xff000000u | (blue << 16) | (green << 8) | red;
            }
        x += g->advance;
    }
}
void Canvas::picture(unsigned x, unsigned y, unsigned width, unsigned height, unsigned source_edge,
                     const std::uint32_t *source) noexcept
{
    frame_drawn = true;
    if (!source || !source_edge || !width || !height || x >= frame_width || y >= frame_height)
        return;
    for (unsigned yy = 0; yy < height && y + yy < frame_height; yy++)
        for (unsigned xx = 0; xx < width && x + xx < frame_width; xx++)
            put_pixel_unchecked(
                pixels_, x + xx, y + yy,
                static_cast<Color>(
                    source[(static_cast<std::size_t>(yy) * source_edge / height) * source_edge +
                           static_cast<std::size_t>(xx) * source_edge / width]));
}

void Canvas::image(unsigned x, unsigned y, unsigned edge, const std::uint32_t *source) noexcept
{
    frame_drawn = true;
    if (!source || !edge || x >= frame_width || y >= frame_height)
        return;
    for (unsigned row = 0; row < edge && y + row < frame_height; row++)
        for (unsigned col = 0; col < edge && x + col < frame_width; col++)
            put_pixel_unchecked(pixels_, x + col, y + row,
                                static_cast<Color>(source[row * edge + col]));
}
namespace
{
[[nodiscard]] std::uint32_t mix_channels(std::uint32_t a, std::uint32_t b, unsigned weight_a) noexcept
{
    std::uint32_t out = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
    {
        const std::uint32_t va = (a >> shift) & 255U, vb = (b >> shift) & 255U;
        out |= ((va * weight_a + vb * (255U - weight_a)) / 255U) << shift;
    }
    return out;
}

[[nodiscard]] unsigned integer_sqrt(unsigned value) noexcept
{
    unsigned root = 0;
    while ((root + 1) * (root + 1) <= value)
        ++root;
    return root;
}
} // namespace

void Canvas::gradient(unsigned x, unsigned y, unsigned width, unsigned height, Color top,
                      Color bottom) noexcept
{
    frame_drawn = true;
    if (!width || !height || x >= frame_width || y >= frame_height)
        return;
    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned limit = height > frame_height - y ? frame_height : y + height;
    for (unsigned row = y; row < limit; ++row)
    {
        const unsigned weight = 255U - (row - y) * 255U / (height > 1 ? height - 1 : 1);
        const auto color = static_cast<Color>(mix_channels(static_cast<std::uint32_t>(top),
                                                          static_cast<std::uint32_t>(bottom), weight));
        for (unsigned column = x; column < right; ++column)
            put_pixel_unchecked(pixels_, column, row, color);
    }
}

void Canvas::blend(unsigned x, unsigned y, unsigned width, unsigned height, Color color,
                   unsigned alpha) noexcept
{
    frame_drawn = true;
    if (!width || !height || x >= frame_width || y >= frame_height || !alpha)
        return;
    if (alpha > 255)
        alpha = 255;
    const unsigned right = width > frame_width - x ? frame_width : x + width;
    const unsigned limit = height > frame_height - y ? frame_height : y + height;
    const auto source = static_cast<std::uint32_t>(color);
    for (unsigned row = y; row < limit; ++row)
        for (unsigned column = x; column < right; ++column)
        {
            auto *d = reinterpret_cast<std::uint32_t *>(reinterpret_cast<unsigned char *>(pixels_) +
                                                        tiled_byte_offset(column, row));
            *d = mix_channels(source, *d, alpha);
        }
}

void Canvas::rounded_picture(unsigned x, unsigned y, unsigned width, unsigned height,
                             unsigned radius, unsigned source_edge,
                             const std::uint32_t *source) noexcept
{
    frame_drawn = true;
    if (!source || !source_edge || !width || !height || x >= frame_width || y >= frame_height)
        return;
    if (radius > width / 2)
        radius = width / 2;
    if (radius > height / 2)
        radius = height / 2;
    const bool one_to_one = source_edge == width && width == height;
    for (unsigned yy = 0; yy < height && y + yy < frame_height; yy++)
    {
        const unsigned edge_distance = yy < height - 1 - yy ? yy : height - 1 - yy;
        unsigned inset = 0;
        if (edge_distance < radius)
        {
            const unsigned d = radius - edge_distance;
            inset = radius - integer_sqrt(radius * radius - d * d);
        }
        if (one_to_one)
        {
            for (unsigned xx = inset; xx + inset < width && x + xx < frame_width; xx++)
                put_pixel_unchecked(pixels_, x + xx, y + yy,
                                    static_cast<Color>(source[static_cast<std::size_t>(yy) * width + xx]));
            continue;
        }
        // Box filter over the source area that maps to this row (at most 3 taps per axis).
        const unsigned sy0 = static_cast<unsigned>(static_cast<std::size_t>(yy) * source_edge / height);
        unsigned sy1 = static_cast<unsigned>(static_cast<std::size_t>(yy + 1) * source_edge / height);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        const unsigned span_y = sy1 - sy0, step_y = (span_y + 2) / 3;
        for (unsigned xx = inset; xx + inset < width && x + xx < frame_width; xx++)
        {
            const unsigned sx0 =
                static_cast<unsigned>(static_cast<std::size_t>(xx) * source_edge / width);
            unsigned sx1 =
                static_cast<unsigned>(static_cast<std::size_t>(xx + 1) * source_edge / width);
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            const unsigned step_x = (sx1 - sx0 + 2) / 3;
            std::uint32_t r = 0, g = 0, b = 0, n = 0;
            for (unsigned sy = sy0; sy < sy1 && sy < source_edge; sy += step_y)
                for (unsigned sx = sx0; sx < sx1 && sx < source_edge; sx += step_x)
                {
                    const std::uint32_t p = source[static_cast<std::size_t>(sy) * source_edge + sx];
                    r += p & 255U;
                    g += (p >> 8) & 255U;
                    b += (p >> 16) & 255U;
                    ++n;
                }
            if (!n)
                continue;
            put_pixel_unchecked(pixels_, x + xx, y + yy,
                                static_cast<Color>(0xff000000u | ((b / n) << 16) | ((g / n) << 8) |
                                                   (r / n)));
        }
    }
}

void Canvas::shadow(unsigned x, unsigned y, unsigned width, unsigned height, unsigned spread,
                    unsigned alpha) noexcept
{
    frame_drawn = true;
    // Soft shadow made of rounded rings that fade outward; shifted down, no overdraw under the card.
    constexpr unsigned step = 3;
    constexpr unsigned corner = 18;
    if (!spread || !width || !height)
        return;
    const unsigned drop = spread / 3;
    const auto black = static_cast<std::uint32_t>(0xff000000u);
    // Horizontal span of a rounded rectangle on one row (empty when the row is outside it).
    auto span = [](int rx, int ry, int rw, int rh, int radius, int row, int &from, int &to) -> bool
    {
        if (row < ry || row >= ry + rh)
            return false;
        if (radius > rw / 2)
            radius = rw / 2;
        if (radius > rh / 2)
            radius = rh / 2;
        const int top = row - ry, bottom = ry + rh - 1 - row;
        const int edge = top < bottom ? top : bottom;
        int inset = 0;
        if (edge < radius)
        {
            const int d = radius - edge;
            inset = radius - static_cast<int>(integer_sqrt(static_cast<unsigned>(radius * radius - d * d)));
        }
        from = rx + inset;
        to = rx + rw - inset;
        return to > from;
    };
    for (unsigned grow = spread; grow >= step; grow -= step)
    {
        const unsigned falloff = spread - grow + step; // 3 .. spread
        const unsigned a = alpha * falloff * falloff / (spread * spread);
        if (!a)
            continue;
        const int ox = static_cast<int>(x) - static_cast<int>(grow);
        const int oy = static_cast<int>(y + drop) - static_cast<int>(grow);
        const int ow = static_cast<int>(width + 2 * grow), oh = static_cast<int>(height + 2 * grow);
        const int inner = static_cast<int>(grow) - static_cast<int>(step);
        const int ix = static_cast<int>(x) - inner, iy = static_cast<int>(y + drop) - inner;
        const int iw = static_cast<int>(width) + 2 * inner, ih = static_cast<int>(height) + 2 * inner;
        for (int row = oy; row < oy + oh; ++row)
        {
            if (row < 0 || row >= static_cast<int>(frame_height))
                continue;
            int from = 0, to = 0;
            if (!span(ox, oy, ow, oh, static_cast<int>(corner + grow), row, from, to))
                continue;
            int ifrom = 0, ito = 0;
            const bool has_inner = span(ix, iy, iw, ih, static_cast<int>(corner + grow - step), row, ifrom, ito);
            for (int column = from; column < to; ++column)
            {
                if (column < 0 || column >= static_cast<int>(frame_width))
                    continue;
                if (has_inner && column >= ifrom && column < ito)
                {
                    column = ito - 1;
                    continue;
                }
                auto *d = reinterpret_cast<std::uint32_t *>(
                    reinterpret_cast<unsigned char *>(pixels_) +
                    tiled_byte_offset(static_cast<unsigned>(column), static_cast<unsigned>(row)));
                *d = mix_channels(black, *d, a);
            }
        }
    }
}

void Canvas::rounded_rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                               unsigned radius, Color color) noexcept
{
    frame_drawn = true;
    if (!width || !height)
        return;
    if (radius > width / 2)
        radius = width / 2;
    if (radius > height / 2)
        radius = height / 2;
    rectangle(x + radius, y, width - 2 * radius, height, color);
    rectangle(x, y + radius, width, height - 2 * radius, color);
    if (radius)
    {
        circle(x + radius, y + radius, radius, color);
        circle(x + width - radius - 1, y + radius, radius, color);
        circle(x + radius, y + height - radius - 1, radius, color);
        circle(x + width - radius - 1, y + height - radius - 1, radius, color);
    }
}

void Canvas::clear(Color color) noexcept
{
    frame_drawn = true;
    fill_rect(pixels_, 0, 0, frame_width, frame_height, color);
}

void Canvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                       Color color) noexcept
{
    frame_drawn = true;
    fill_rect(pixels_, x, y, width, height, color);
}

void Canvas::circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept
{
    frame_drawn = true;
    fill_circle(pixels_, center_x, center_y, radius, color);
}

void Canvas::triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                      Color color) noexcept
{
    frame_drawn = true;
    fill_triangle(pixels_, center_x, top, half_width, height, color);
}

void Canvas::text(unsigned x, unsigned y, std::string_view value, unsigned scale,
                  Color color) noexcept
{
    frame_drawn = true;
    draw_text(pixels_, x, y, value, scale, color);
}

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept
{
    copy_message(destination, fallback);
    File descriptor{open(path, 0)};
    if (!descriptor.valid() || destination.empty())
        return;

    const long count = read(descriptor.get(), destination.data(), destination.size() - 1);
    if (count <= 0)
        return;

    std::size_t length = static_cast<std::size_t>(count);
    while (length > 0 && (destination[length - 1] == '\r' || destination[length - 1] == '\n'))
        --length;
    destination[length] = '\0';
}

[[noreturn]] void run(DrawScene draw, std::string_view ready_message) noexcept
{
    detail::present(draw, ready_message, false);
}

[[noreturn]] void run_frames(DrawScene draw, std::string_view ready_message) noexcept
{
    detail::present(draw, ready_message, true);
}

[[noreturn]] void detail::present(DrawScene draw, std::string_view ready_message,
                                  bool every_frame) noexcept
{
    if (!verify_unique_ownership())
        halt("Hello World: unique ownership failed");
    if (draw == nullptr)
        halt("Hello World: scene callback missing");

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0)
        halt("Hello World: sceVideoOutOpen failed");

    const std::size_t pool_size = sceKernelGetDirectMemorySize();
    if (pool_size < memory_bytes)
        halt("Hello World: insufficient direct memory");

    std::int64_t physical_address = 0;
    int result =
        sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool_size), memory_bytes,
                                      memory_alignment, memory_type_wc_garlic, &physical_address);
    if (result < 0)
        halt("Hello World: direct-memory allocation failed");

    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical_address,
                                      memory_alignment);
    if (result < 0)
        halt("Hello World: direct-memory mapping failed");

    // Draw into ordinary cached memory (cheap read-modify-write for shadows and text), then copy the
    // finished frame to the write-combined video buffers.
    static std::uint32_t scene[frame_bytes / sizeof(std::uint32_t)];
    constexpr std::size_t used_bytes = static_cast<std::size_t>((frame_width + 127U) >> 7) *
                                       ((frame_height + 127U) >> 7) * 0x10000U;
    auto *second_frame = static_cast<std::uint8_t *>(mapped) + frame_bytes;
    Canvas scene_canvas{scene};
    draw(scene_canvas);
    std::memcpy(mapped, scene, used_bytes);
    std::memcpy(second_frame, scene, used_bytes);
    flush_range(mapped, used_bytes);
    flush_range(second_frame, used_bytes);

    std::array<VideoBuffer, 2> buffers{{
        {mapped, nullptr, nullptr, nullptr},
        {second_frame, nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, frame_width,
                                   frame_height, 0, 0, 0);

    result = sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(),
                                         static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                         nullptr);
    if (result < 0)
        halt("Hello World: buffer registration failed");
    if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)
        halt("Hello World: initial flip failed");

    (void)sceVideoOutWaitVblank(video);
    notify(ready_message);

    // The scene callback redraws into the cached buffer only when its state changed; a changed frame
    // is copied to both video buffers (one per flip), idle frames cost only the vblank wait.
    if (every_frame)
    {
        std::size_t shown = 0;
        int stale = 0;
        unsigned copies = 0;
        for (std::int64_t frame = 2;; ++frame)
        {
            if (frame % 600 == 0)
            {
                char line[96];
                std::snprintf(line, sizeof line, "ESTANTE: loop ativo frames=%lld copias=%u\n",
                              static_cast<long long>(frame), copies);
                (void)sceKernelDebugOutText(0, line);
            }
            frame_drawn = false;
            Canvas canvas{scene};
            draw(canvas);
            if (frame_drawn)
                stale = 2;
            if (stale > 0)
            {
                const std::size_t back = shown ^ 1U;
                std::memcpy(buffers[back].data, scene, used_bytes);
                flush_range(buffers[back].data, used_bytes);
                if (sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(back), 1, frame) < 0)
                {
                    (void)sceKernelUsleep(4000); // the previous flip is still queued
                    (void)sceVideoOutWaitVblank(video);
                    continue; // try this frame again; do not count it
                }
                shown = back;
                --stale;
                ++copies;
            }
            else
            {
                // Idle: keep presenting the same buffer every vblank, as the original loop did, so the
                // system always sees a live video client (PS button, overlays, network dialogs).
                (void)sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(shown), 1, frame);
            }
            (void)sceVideoOutWaitVblank(video);
        }
    }

    // Returning from main or calling exit crashes this launch context.
    for (;;)
        (void)sceKernelUsleep(1000000);
}
} // namespace ps5::demo
