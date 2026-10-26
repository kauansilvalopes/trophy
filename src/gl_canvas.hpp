// Estante - GPU canvas: the drawing calls the screens use, recorded into the kit's draw list.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same call surface as the CPU canvas in demo_renderer.hpp (rectangle, rounded_rectangle, circle,
// gradient, blend, shadow, rounded_picture, label, measure, clear, dim), so the screen code in
// main.cpp draws through either. Positions are in the 1920x1080 virtual canvas, y down.
#pragma once

#include "demo_renderer.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/gl_batch.hpp"
#include "gfx/renderer.hpp"
#include "gl3d.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace estante
{
class GlCanvas final
{
  public:
    using Color = ps5::demo::Color;

    // Starts a frame: the draw lists are cleared and receive everything drawn until the next begin().
    void begin(hui::gfx::GlBatch *batch, const hui::gfx::Font *font, std::uint32_t font_texture) noexcept;
    // Queues the frame on the renderer: the 2D lists with the 3D cases and discs between them, in call order.
    void submit(hui::gfx::Renderer &renderer) noexcept;
    // A 3D shape drawn after everything recorded so far and before whatever is recorded next.
    void case3d(const CaseOp &op) noexcept;
    void disc3d(const DiscOp &op) noexcept;
    // Draws the next calls into the 640x960 back-face picture of a case (y down, same calls as the screens use);
    // end_back() queues the render. back_texture() is the picture, to be used with CaseOp::flip_back.
    void begin_back() noexcept;
    void end_back() noexcept;
    std::uint32_t back_texture() const noexcept
    {
        return back_target_.texture();
    }
    static constexpr int back_width = 640, back_height = 960;
    // The same for the 960x128 spine picture (text runs along its width, top to bottom on the case).
    void begin_spine() noexcept;
    void end_spine() noexcept;
    std::uint32_t spine_texture() const noexcept
    {
        return spine_target_.texture();
    }
    static constexpr int spine_width = 960, spine_height = 128;
    bool has_3d() const noexcept
    {
        return gl3d_.ready();
    }

    void clear(Color color) noexcept;
    void dim() noexcept;
    void label(unsigned x, unsigned y, std::string_view value, unsigned size, Color color,
               unsigned max_width = 1800) noexcept;
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

    // Tells the canvas that the pixels behind this pointer changed (a cover slot was refilled).
    void invalidate(const std::uint32_t *pixels) noexcept;

    // Extra drawing the GPU path offers beyond the CPU canvas's calls.
    hui::gfx::DrawList &list() noexcept
    {
        return *list_;
    }
    std::uint32_t texture_for(const std::uint32_t *pixels, unsigned edge) noexcept;
    std::uint32_t texture_for(const std::uint32_t *pixels, unsigned width, unsigned height) noexcept;

  private:
    struct CachedTexture
    {
        const std::uint32_t *pixels = nullptr;
        unsigned edge = 0, height = 0;
        std::uint32_t texture = 0;
        bool dirty = false;
    };

    struct Item
    {
        Gl3D *gl = nullptr;
        int kind = 0; // 0 case, 1 disc, 2 render the back face, 3 render the spine
        GlCanvas *owner = nullptr;
        CaseOp c;
        DiscOp d;
    };
    static void run_item(void *user, int width, int height) noexcept;
    void split() noexcept;
    void render_back(int width, int height) noexcept;
    void render_spine(int width, int height) noexcept;
    void render_to(hui::gfx::Canvas &target, hui::gfx::DrawList &list, int w, int h, int width, int height) noexcept;

    static constexpr std::size_t max_items = 30;
    hui::gfx::DrawList lists_[max_items + 1];
    std::vector<Item> items_;
    std::size_t segment_ = 0;
    Gl3D gl3d_;
    hui::gfx::Canvas back_target_, spine_target_;
    hui::gfx::DrawList spine_list_;
    hui::gfx::DrawList back_list_;
    hui::gfx::DrawList *saved_list_ = nullptr;
    bool gl3d_tried_ = false;
    hui::gfx::DrawList *list_ = nullptr;
    hui::gfx::GlBatch *batch_ = nullptr;
    const hui::gfx::Font *font_ = nullptr;
    std::uint32_t font_texture_ = 0;
    std::vector<CachedTexture> textures_;
};

// Converts the packed 0xAABBGGRR colour the screens use to the kit's float colour.
hui::gfx::Color to_gfx(ps5::demo::Color color) noexcept;
} // namespace estante
