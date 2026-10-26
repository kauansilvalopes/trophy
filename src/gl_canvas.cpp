// Estante - GPU canvas. SPDX-License-Identifier: GPL-3.0-or-later
#include "gl_canvas.hpp"

#include <GL/glcorearb.h>

#include <algorithm>
#include <string>

namespace estante
{
using hui::gfx::Rect;

hui::gfx::Color to_gfx(ps5::demo::Color color) noexcept
{
    const auto c = static_cast<std::uint32_t>(color);
    return {static_cast<float>(c & 0xff) / 255.0f, static_cast<float>((c >> 8) & 0xff) / 255.0f,
            static_cast<float>((c >> 16) & 0xff) / 255.0f, static_cast<float>(c >> 24) / 255.0f};
}

namespace
{
// One entry per pixel buffer: every cover slot owns three (picture, thumbnail, tall picture), so 32 slots already
// fill 96. When the table is full the oldest texture is deleted at once, and a card drawn earlier in the same
// frame that still points at it shows another game's cover; leave room for the wide art, icons and case faces.
constexpr std::size_t max_cached_textures = 256;

Rect rect_of(unsigned x, unsigned y, unsigned w, unsigned h) noexcept
{
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
}

// The few symbols the button hints use that the baked font does not carry are drawn as shapes.
bool is_shape_glyph(std::uint32_t cp) noexcept
{
    return cp == 0x25b3 || cp == 0x25cb || cp == 0x25a1 || cp == 0x2605 || cp == 0xd7;
}
} // namespace

void GlCanvas::begin(hui::gfx::GlBatch *batch, const hui::gfx::Font *font, std::uint32_t font_texture) noexcept
{
    batch_ = batch;
    font_ = font;
    font_texture_ = font_texture;
    if (!gl3d_tried_)
    {
        gl3d_tried_ = true;
        items_.reserve(max_items);
        if (gl3d_.init())
        {
            back_target_.create(back_width, back_height, 1);
            spine_target_.create(spine_width, spine_height, 1);
        }
    }
    items_.clear();
    segment_ = 0;
    list_ = &lists_[0];
    list_->clear();
}

void GlCanvas::split() noexcept
{
    segment_++;
    list_ = &lists_[segment_];
    list_->clear();
}

void GlCanvas::case3d(const CaseOp &op) noexcept
{
    if (!gl3d_.ready() || items_.size() >= max_items)
        return;
    Item item;
    item.gl = &gl3d_;
    item.kind = 0;
    item.c = op;
    items_.push_back(item);
    split();
}

void GlCanvas::disc3d(const DiscOp &op) noexcept
{
    if (!gl3d_.ready() || items_.size() >= max_items)
        return;
    Item item;
    item.gl = &gl3d_;
    item.kind = 1;
    item.d = op;
    items_.push_back(item);
    split();
}

void GlCanvas::run_item(void *user, int width, int height) noexcept
{
    auto *item = static_cast<Item *>(user);
    if (item->kind == 2)
    {
        item->owner->render_back(width, height);
        return;
    }
    if (item->kind == 3)
    {
        item->owner->render_spine(width, height);
        return;
    }
    item->gl->begin(width, height);
    if (item->kind == 0)
        item->gl->draw(item->c);
    else
        item->gl->draw(item->d);
    item->gl->end();
}

void GlCanvas::begin_back() noexcept
{
    saved_list_ = list_;
    back_list_.clear();
    list_ = &back_list_;
}

void GlCanvas::end_back() noexcept
{
    list_ = saved_list_;
    if (!gl3d_.ready() || items_.size() >= max_items)
        return;
    Item item;
    item.gl = &gl3d_;
    item.kind = 2;
    item.owner = this;
    items_.push_back(item);
    split();
}

void GlCanvas::render_to(hui::gfx::Canvas &target, hui::gfx::DrawList &list, int w, int h, int width,
                         int height) noexcept
{
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    target.bind();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.04f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    batch_->draw(list, hui::gfx::fit_viewport(w, h, w, h), w, h);
    target.unbind(static_cast<GLuint>(previous));
    glViewport(0, 0, width, height);
}

void GlCanvas::render_back(int width, int height) noexcept
{
    render_to(back_target_, back_list_, back_width, back_height, width, height);
}

void GlCanvas::render_spine(int width, int height) noexcept
{
    render_to(spine_target_, spine_list_, spine_width, spine_height, width, height);
}

void GlCanvas::begin_spine() noexcept
{
    saved_list_ = list_;
    spine_list_.clear();
    list_ = &spine_list_;
}

void GlCanvas::end_spine() noexcept
{
    list_ = saved_list_;
    if (!gl3d_.ready() || items_.size() >= max_items)
        return;
    Item item;
    item.gl = &gl3d_;
    item.kind = 3;
    item.owner = this;
    items_.push_back(item);
    split();
}

void GlCanvas::submit(hui::gfx::Renderer &renderer) noexcept
{
    for (std::size_t i = 0; i <= segment_; i++)
    {
        renderer.draw(lists_[i]);
        if (i < items_.size())
            renderer.custom(&GlCanvas::run_item, &items_[i]);
    }
}

void GlCanvas::clear(Color color) noexcept
{
    list_->rounded_rect({0, 0, 1920, 1080}, 0, to_gfx(color));
}

void GlCanvas::dim() noexcept
{
    list_->rounded_rect({0, 0, 1920, 1080}, 0, {0, 0, 0, 0.5f});
}

void GlCanvas::gradient(unsigned x, unsigned y, unsigned width, unsigned height, Color top,
                        Color bottom) noexcept
{
    list_->gradient_rect(rect_of(x, y, width, height), 0, to_gfx(top), to_gfx(bottom));
}

void GlCanvas::blend(unsigned x, unsigned y, unsigned width, unsigned height, Color color,
                     unsigned alpha) noexcept
{
    hui::gfx::Color c = to_gfx(color);
    c.a *= static_cast<float>(std::min(alpha, 255u)) / 255.0f;
    list_->rounded_rect(rect_of(x, y, width, height), 0, c);
}

void GlCanvas::shadow(unsigned x, unsigned y, unsigned width, unsigned height, unsigned spread,
                      unsigned alpha) noexcept
{
    if (!spread || !width || !height)
        return;
    list_->shadow(rect_of(x, y + spread / 3, width, height), 16.0f, static_cast<float>(spread),
                  {0, 0, 0, static_cast<float>(std::min(alpha, 255u)) / 255.0f});
}

void GlCanvas::rounded_rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                                 unsigned radius, Color color) noexcept
{
    list_->rounded_rect(rect_of(x, y, width, height), static_cast<float>(radius), to_gfx(color));
}

void GlCanvas::rectangle(unsigned x, unsigned y, unsigned width, unsigned height,
                         Color color) noexcept
{
    list_->rounded_rect(rect_of(x, y, width, height), 0, to_gfx(color));
}

void GlCanvas::circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept
{
    list_->circle(static_cast<float>(center_x), static_cast<float>(center_y),
                  static_cast<float>(radius), to_gfx(color));
}

std::uint32_t GlCanvas::texture_for(const std::uint32_t *pixels, unsigned edge) noexcept
{
    return texture_for(pixels, edge, edge);
}

std::uint32_t GlCanvas::texture_for(const std::uint32_t *pixels, unsigned width, unsigned height) noexcept
{
    for (auto &t : textures_)
        if (t.pixels == pixels && t.edge == width && t.height == height)
        {
            if (t.dirty)
            {
                const GLuint old = t.texture;
                glDeleteTextures(1, &old);
                t.texture = batch_->create_texture(static_cast<int>(width), static_cast<int>(height),
                                                   reinterpret_cast<const std::uint8_t *>(pixels));
                t.dirty = false;
            }
            return t.texture;
        }
    if (textures_.size() >= max_cached_textures)
    {
        const GLuint old = textures_.front().texture;
        glDeleteTextures(1, &old);
        textures_.erase(textures_.begin());
    }
    CachedTexture t;
    t.pixels = pixels;
    t.edge = width;
    t.height = height;
    t.texture = batch_->create_texture(static_cast<int>(width), static_cast<int>(height),
                                       reinterpret_cast<const std::uint8_t *>(pixels));
    textures_.push_back(t);
    return t.texture;
}

void GlCanvas::invalidate(const std::uint32_t *pixels) noexcept
{
    for (auto &t : textures_)
        if (t.pixels == pixels)
            t.dirty = true;
}

void GlCanvas::rounded_picture(unsigned x, unsigned y, unsigned width, unsigned height,
                               unsigned radius, unsigned source_edge,
                               const std::uint32_t *pixels) noexcept
{
    if (!pixels || !source_edge || !width || !height)
        return;
    const std::uint32_t texture = texture_for(pixels, source_edge);
    list_->image(texture, rect_of(x, y, width, height), hui::gfx::kFullUv, {1, 1, 1, 1},
                 static_cast<float>(radius));
}

unsigned GlCanvas::measure(std::string_view value, unsigned size) noexcept
{
    if (!font_)
        return 0;
    float width = 0.0f;
    std::string segment;
    std::size_t i = 0;
    while (i < value.size())
    {
        const std::size_t before = i;
        const std::uint32_t cp = hui::gfx::next_codepoint(value, &i);
        if (is_shape_glyph(cp))
        {
            width += font_->measure(segment, static_cast<float>(size));
            segment.clear();
            width += static_cast<float>(size) * 0.95f;
        }
        else
            segment.append(value.substr(before, i - before));
    }
    width += font_->measure(segment, static_cast<float>(size));
    return static_cast<unsigned>(width + 0.5f);
}

void GlCanvas::label(unsigned x, unsigned y, std::string_view value, unsigned size, Color color,
                     unsigned max_width) noexcept
{
    if (!font_ || value.empty())
        return;
    const hui::gfx::Color fill = to_gfx(color);
    const float fsize = static_cast<float>(size);
    const float baseline = static_cast<float>(y) + fsize;
    float pen = static_cast<float>(x);
    std::string segment;
    bool has_shape = false;
    for (std::size_t i = 0; i < value.size();)
    {
        const std::size_t before = i;
        if (is_shape_glyph(hui::gfx::next_codepoint(value, &i)))
        {
            has_shape = true;
            break;
        }
        (void)before;
    }
    auto flush = [&]()
    {
        if (segment.empty())
            return;
        pen += list_->text(*font_, font_texture_, segment, pen, baseline, fsize, fill);
        segment.clear();
    };
    if (!has_shape)
    {
        const std::string fitted = font_->fit(value, fsize, static_cast<float>(max_width));
        list_->text(*font_, font_texture_, fitted, pen, baseline, fsize, fill);
        return;
    }
    for (std::size_t i = 0; i < value.size();)
    {
        const std::size_t before = i;
        const std::uint32_t cp = hui::gfx::next_codepoint(value, &i);
        if (!is_shape_glyph(cp))
        {
            segment.append(value.substr(before, i - before));
            continue;
        }
        flush();
        const float box = fsize * 0.72f;
        const float left = pen + fsize * 0.08f;
        const float top = baseline - fsize * 0.80f;
        const float stroke = std::max(2.0f, fsize / 13.0f);
        switch (cp)
        {
        case 0x25cb:
            list_->ring(left + box * 0.5f, top + box * 0.5f, box * 0.5f, stroke, fill);
            break;
        case 0x25a1:
            list_->bordered_rect({left, top, box, box}, 2.0f, {0, 0, 0, 0}, stroke, fill);
            break;
        case 0x25b3:
            list_->triangle({left, top, box, box * 0.9f}, fill, stroke);
            break;
        case 0xd7:
            list_->line(left, top, left + box, top + box, stroke, fill);
            list_->line(left + box, top, left, top + box, stroke, fill);
            break;
        case 0x2605:
            list_->star(left + box * 0.5f, top + box * 0.5f, box * 0.55f, fill);
            break;
        default:
            break;
        }
        pen += fsize * 0.95f;
    }
    flush();
}
} // namespace estante
