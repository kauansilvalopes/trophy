// Estante - small 3D pass for game cases and discs. SPDX-License-Identifier: GPL-3.0-or-later
//
// The 2D interface is drawn by the kit's draw list. This module draws a few textured meshes (a case, a disc) in
// perspective between two 2D layers. World units are virtual pixels: the plane z = 0 maps exactly onto the 1920x1080
// canvas, z grows toward the viewer, y grows downward. There is no depth buffer: a case is convex, so culling back
// faces is enough, and the caller draws shapes back to front.
#pragma once

#include <cstdint>

namespace estante
{
struct Mat4
{
    float m[16]; // column major
};
Mat4 mat4_identity() noexcept;
Mat4 mat4_multiply(const Mat4 &a, const Mat4 &b) noexcept;
Mat4 mat4_translate(float x, float y, float z) noexcept;
Mat4 mat4_scale(float x, float y, float z) noexcept;
Mat4 mat4_rotate_x(float radians) noexcept;
Mat4 mat4_rotate_y(float radians) noexcept;
Mat4 mat4_rotate_z(float radians) noexcept;

// A jewel case: a box with the cover on the front, a second picture on the back and plain sides.
struct CaseOp
{
    float x = 960.0f, y = 540.0f, z = 0.0f; // centre
    float width = 300.0f, height = 450.0f, depth = 56.0f;
    float yaw = 0.0f;                        // turn about the vertical axis, radians
    float pitch = 0.0f;                      // tilt about the horizontal axis
    float roll = 0.0f;                       // turn about the axis out of the screen
    float scale = 1.0f;
    std::uint32_t front = 0, back = 0;       // GL texture names (0 draws a flat colour)
    std::uint32_t spine = 0;                 // picture for the left edge (rendered into a framebuffer)
    float shine = 0.0f;                      // glint on the front that slides as the case turns
    bool flip_back = false;                  // the back texture was rendered into a framebuffer (rows upside down)
    float side_r = 0.10f, side_g = 0.11f, side_b = 0.13f;
    float alpha = 1.0f;
    float shade = 1.0f;                      // multiplies the colour (dim cases far from the focus)
    bool reflection = false;                 // draw it mirrored about floor_y, fading out
    float floor_y = 0.0f, fade_length = 120.0f;
    int part = 0;                            // 0 the whole case, 1 everything but the front, 2 only the front
    float opening = 0.0f;                    // 0 closed .. 1 the front cover swung open on its left hinge
};

// A disc: label picture on the face, silver rim, hub and hole.
struct DiscOp
{
    float x = 960.0f, y = 540.0f, z = 0.0f;
    float radius = 185.0f;
    float spin = 0.0f;                       // turn in the plane of the disc, radians
    float yaw = 0.0f;                        // tilt about the vertical axis
    std::uint32_t label = 0;
    float alpha = 1.0f;
    float sheen = 0.0f;                      // the bright bands of a real disc, fixed while the disc spins
    float tint_r = 0.82f, tint_g = 0.84f, tint_b = 0.90f; // colour of the silver
    float hole_r = 0.06f, hole_g = 0.065f, hole_b = 0.08f; // what shows through the centre hole
};

class Gl3D
{
  public:
    bool init() noexcept;
    void release() noexcept;
    // Called once per frame by the renderer's custom layer: sets the state the passes need.
    void begin(int surface_width, int surface_height) noexcept;
    // Restores the state the 2D batch expects (culling off).
    void end() noexcept;
    void draw(const CaseOp &op) noexcept;
    void draw(const DiscOp &op) noexcept;
    bool ready() const noexcept
    {
        return program_ != 0;
    }

  private:
    struct Vertex
    {
        float x, y, z, u, v, nx, ny, nz;
    };
    void draw_range(const Mat4 &model, int first, int count, std::uint32_t texture, float r, float g, float b,
                    float alpha, const CaseOp *reflect) noexcept;

    unsigned program_ = 0, vao_ = 0, buffer_ = 0;
    int surface_width_ = 1920, surface_height_ = 1080;
    Mat4 projection_{};
    // uniform locations
    float flip_ = 0.0f, shine_ = 0.0f, sheen_ = 0.0f, spin_ = 0.0f;
    int u_shine_ = -1, u_sheen_ = -1, u_spin_ = -1;
    int spine_first_ = 0;
    int u_flip_ = -1;
    int u_mvp_ = -1, u_model_ = -1, u_tex_ = -1, u_color_ = -1, u_textured_ = -1, u_ambient_ = -1, u_fade_ = -1,
        u_light_ = -1;
    int box_front_ = 0, box_back_ = 6, box_sides_ = 12, box_sides_count_ = 24, disc_first_ = 36, disc_count_ = 0;
    int cover_first_ = 0, cover_count_ = 0;
};
} // namespace estante
