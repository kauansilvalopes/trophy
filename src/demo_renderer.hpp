/*
 * ps5-native-app-boilerplate - Small CPU-rendered demonstration API.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keeps PS5 VideoOut setup and the bitmap font out of the starter main file.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace ps5::demo
{
enum class Color : std::uint32_t
{
    background = UINT32_C(0xff190d0a),
    panel = UINT32_C(0xff301f17),
    white = UINT32_C(0xffffffff),
    cyan = UINT32_C(0xffffff00),
    magenta = UINT32_C(0xffff00ff),
    yellow = UINT32_C(0xff00ffff),
};

class Canvas;
using DrawScene = void (*)(Canvas &) noexcept;
// Draws the scene once and keeps it on screen.
[[noreturn]] void run(DrawScene draw, std::string_view ready_message) noexcept;
// Calls draw for every frame, for a scene that changes. The callback redraws the whole
// frame; drawing on the CPU, a full 1920x1080 frame takes longer than one display refresh.
[[noreturn]] void run_frames(DrawScene draw, std::string_view ready_message) noexcept;

namespace detail
{
[[noreturn]] void present(DrawScene draw, std::string_view ready_message,
                          bool every_frame) noexcept;
}

class Canvas final
{
  public:
#ifdef ESTANTE_HOST_PREVIEW
    static Canvas host_preview(std::uint32_t *pixels) noexcept
    {
        return Canvas(pixels);
    }
#endif
    void clear(Color color) noexcept;
    void dim() noexcept;
    void label(unsigned x, unsigned y, std::string_view value, unsigned size, Color color,
               unsigned max_width = 1800) noexcept;
    void picture(unsigned x, unsigned y, unsigned width, unsigned height, unsigned source_edge,
                 const std::uint32_t *pixels) noexcept;
    void image(unsigned x, unsigned y, unsigned edge, const std::uint32_t *pixels) noexcept;
    // Vertical gradient, translucent fill, rounded scaled picture and soft shadow (CPU, bounded).
    // Width in pixels that label() would use for this text (for centring).
    unsigned measure(std::string_view value, unsigned size) noexcept;
    void gradient(unsigned x, unsigned y, unsigned width, unsigned height, Color top,
                  Color bottom) noexcept;
    void blend(unsigned x, unsigned y, unsigned width, unsigned height, Color color,
               unsigned alpha) noexcept;
    void rounded_picture(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                         unsigned source_edge, const std::uint32_t *pixels) noexcept;
    void shadow(unsigned x, unsigned y, unsigned width, unsigned height, unsigned spread,
                unsigned alpha) noexcept;
    void rounded_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                           Color color) noexcept;
    void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, Color color) noexcept;
    void circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept;
    void triangle(unsigned center_x, unsigned top, unsigned half_width, unsigned height,
                  Color color) noexcept;
    void text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color color) noexcept;

  private:
    explicit Canvas(std::uint32_t *pixels) noexcept : pixels_{pixels}
    {
    }

    std::uint32_t *pixels_;

    friend void detail::present(DrawScene draw, std::string_view ready_message,
                                bool every_frame) noexcept;
};

void read_asset_text(const char *path, std::span<char> destination,
                     std::string_view fallback) noexcept;
} // namespace ps5::demo
