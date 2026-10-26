// Estante - small 3D pass for game cases and discs. SPDX-License-Identifier: GPL-3.0-or-later
#include "gl3d.hpp"

#include "gfx/gl_program.hpp"

#include <GL/glcorearb.h>

#include <cmath>
#include <vector>

namespace estante
{
Mat4 mat4_identity() noexcept
{
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mat4_multiply(const Mat4 &a, const Mat4 &b) noexcept
{
    Mat4 r{};
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
        {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
                v += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = v;
        }
    return r;
}

Mat4 mat4_translate(float x, float y, float z) noexcept
{
    Mat4 r = mat4_identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 mat4_scale(float x, float y, float z) noexcept
{
    Mat4 r = mat4_identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

Mat4 mat4_rotate_x(float a) noexcept
{
    Mat4 r = mat4_identity();
    r.m[5] = std::cos(a);
    r.m[6] = std::sin(a);
    r.m[9] = -std::sin(a);
    r.m[10] = std::cos(a);
    return r;
}

Mat4 mat4_rotate_y(float a) noexcept
{
    Mat4 r = mat4_identity();
    r.m[0] = std::cos(a);
    r.m[2] = -std::sin(a);
    r.m[8] = std::sin(a);
    r.m[10] = std::cos(a);
    return r;
}

Mat4 mat4_rotate_z(float a) noexcept
{
    Mat4 r = mat4_identity();
    r.m[0] = std::cos(a);
    r.m[1] = std::sin(a);
    r.m[4] = -std::sin(a);
    r.m[5] = std::cos(a);
    return r;
}

namespace
{
const char *vertex_source = R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform float u_flip;
out vec2 v_uv;
out vec3 v_normal;
out float v_y;
void main()
{
    vec4 world = u_model * vec4(a_pos, 1.0);
    v_uv = vec2(a_uv.x, u_flip > 0.5 ? 1.0 - a_uv.y : a_uv.y);
    v_normal = mat3(u_model) * a_normal;
    v_y = world.y;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

const char *fragment_source = R"(
in vec2 v_uv;
in vec3 v_normal;
in float v_y;
uniform sampler2D u_tex;
uniform vec4 u_color;
uniform int u_textured;
uniform float u_ambient;
uniform vec3 u_light;
uniform vec2 u_fade;
uniform float u_shine;
uniform float u_sheen;
uniform float u_spin;
out vec4 frag;
void main()
{
    vec4 base = u_textured != 0 ? texture(u_tex, v_uv) : u_color;
    float lit = max(dot(normalize(v_normal), normalize(u_light)), 0.0);
    float shade = u_ambient + (1.0 - u_ambient) * lit;
    vec4 c = vec4(base.rgb * shade, base.a * u_color.a);
    if (u_textured != 0)
        c.rgb *= u_color.rgb;
    if (u_shine > 0.0)
    {
        vec3 hv = normalize(normalize(u_light) + vec3(0.0, 0.0, 1.0));
        c.rgb += vec3(pow(max(dot(normalize(v_normal), hv), 0.0), 36.0)) * u_shine;
    }
    if (u_sheen > 0.0)
    {
        vec2 q = v_uv - 0.5;
        float band = pow(max(cos(2.0 * (atan(q.y, q.x) + u_spin)), 0.0), 10.0);
        c.rgb += vec3(band) * u_sheen * (0.25 + length(q));
    }
    if (u_fade.y > 0.0)
    {
        float f = clamp(1.0 - (v_y - u_fade.x) / u_fade.y, 0.0, 1.0);
        c.a *= f * f;
    }
    frag = c;
}
)";
} // namespace

bool Gl3D::init() noexcept
{
    program_ = hui::gfx::build_program("estante3d", vertex_source, fragment_source);
    if (!program_)
        return false;
    u_mvp_ = glGetUniformLocation(program_, "u_mvp");
    u_model_ = glGetUniformLocation(program_, "u_model");
    u_tex_ = glGetUniformLocation(program_, "u_tex");
    u_color_ = glGetUniformLocation(program_, "u_color");
    u_textured_ = glGetUniformLocation(program_, "u_textured");
    u_ambient_ = glGetUniformLocation(program_, "u_ambient");
    u_fade_ = glGetUniformLocation(program_, "u_fade");
    u_light_ = glGetUniformLocation(program_, "u_light");
    u_flip_ = glGetUniformLocation(program_, "u_flip");
    u_shine_ = glGetUniformLocation(program_, "u_shine");
    u_sheen_ = glGetUniformLocation(program_, "u_sheen");
    u_spin_ = glGetUniformLocation(program_, "u_spin");

    std::vector<Vertex> v;
    auto quad = [&](const float p[4][3], float nx, float ny, float nz, bool reversed = false)
    {
        static const float uv[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}}; // TL, BL, BR, TR
        const int order[6] = {0, 1, 2, 0, 2, 3}, flipped[6] = {0, 2, 1, 0, 3, 2};
        for (int k = 0; k < 6; k++)
        {
            const int i = reversed ? flipped[k] : order[k];
            v.push_back({p[i][0], p[i][1], p[i][2], uv[i][0], uv[i][1], nx, ny, nz});
        }
    };
    const float front[4][3] = {{-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {.5f, .5f, .5f}, {.5f, -.5f, .5f}};
    const float back[4][3] = {{.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, -.5f}};
    const float left[4][3] = {{-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, -.5f}};
    const float right[4][3] = {{.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f}, {.5f, -.5f, .5f}};
    const float top[4][3] = {{.5f, -.5f, -.5f}, {.5f, -.5f, .5f}, {-.5f, -.5f, .5f}, {-.5f, -.5f, -.5f}};
    const float bottom[4][3] = {{.5f, .5f, .5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f}, {-.5f, .5f, .5f}};
    const float tray[4][3] = {{-.45f, -.46f, -.44f}, {-.45f, .46f, -.44f}, {.45f, .46f, -.44f}, {.45f, -.46f, -.44f}};
    quad(front, 0, 0, 1);
    box_front_ = 0;
    quad(back, 0, 0, -1);
    box_back_ = 6;
    quad(left, -1, 0, 0, true);
    box_sides_ = 12;
    quad(right, 1, 0, 0, true);
    quad(top, 0, -1, 0, true);
    quad(bottom, 0, 1, 0, true);
    box_sides_count_ = 24;
    cover_first_ = static_cast<int>(v.size());
    quad(tray, 0, 0, 1);
    cover_count_ = 6;
    // The left edge of the case (the spine) as its own quad so a picture can run top to bottom along it.
    spine_first_ = static_cast<int>(v.size());
    {
        const float pos[4][3] = {{-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, -.5f}};
        const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const int order[6] = {0, 2, 1, 0, 3, 2};
        for (int i : order)
            v.push_back({pos[i][0], pos[i][1], pos[i][2], uvs[i][0], uvs[i][1], -1, 0, 0});
    }
    disc_first_ = static_cast<int>(v.size());
    const int segments = 96;
    for (int i = 0; i < segments; i++)
    {
        // Counter-clockwise on screen with y down means the angle decreases.
        const float a0 = -6.2831853f * static_cast<float>(i) / segments;
        const float a1 = -6.2831853f * static_cast<float>(i + 1) / segments;
        v.push_back({0, 0, 0, 0.5f, 0.5f, 0, 0, 1});
        v.push_back({std::cos(a0) * .5f, std::sin(a0) * .5f, 0, 0.5f + std::cos(a0) * .5f, 0.5f + std::sin(a0) * .5f, 0, 0, 1});
        v.push_back({std::cos(a1) * .5f, std::sin(a1) * .5f, 0, 0.5f + std::cos(a1) * .5f, 0.5f + std::sin(a1) * .5f, 0, 0, 1});
    }
    disc_count_ = segments * 3;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(Vertex)), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(20));
    glBindVertexArray(0);
    return true;
}

void Gl3D::release() noexcept
{
    if (buffer_)
        glDeleteBuffers(1, &buffer_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    if (program_)
        glDeleteProgram(program_);
    buffer_ = vao_ = program_ = 0;
}

void Gl3D::begin(int surface_width, int surface_height) noexcept
{
    surface_width_ = surface_width;
    surface_height_ = surface_height;
    const float fovy = 30.0f * 3.14159265f / 180.0f;
    const float f = 1.0f / std::tan(fovy * 0.5f);
    const float aspect = static_cast<float>(surface_width) / static_cast<float>(surface_height);
    const float near_plane = 100.0f, far_plane = 6000.0f;
    Mat4 p{};
    p.m[0] = f / aspect;
    p.m[5] = f;
    p.m[10] = -(far_plane + near_plane) / (far_plane - near_plane);
    p.m[11] = -1.0f;
    p.m[14] = -2.0f * far_plane * near_plane / (far_plane - near_plane);
    // eye = (x - 960, 540 - y, z - d): pixel space with y down, plane z = 0 maps 1:1 to the canvas.
    const float d = 540.0f * f;
    Mat4 view = mat4_identity();
    view.m[5] = -1.0f;
    view.m[12] = -960.0f;
    view.m[13] = 540.0f;
    view.m[14] = -d;
    projection_ = mat4_multiply(p, view);
    glViewport(0, 0, surface_width, surface_height);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glUniform1i(u_tex_, 0);
    glUniform3f(u_light_, -0.35f, -0.55f, 0.75f);
}

void Gl3D::end() noexcept
{
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glBindVertexArray(0);
}

void Gl3D::draw_range(const Mat4 &model, int first, int count, std::uint32_t texture, float r, float g, float b,
                      float alpha, const CaseOp *reflect) noexcept
{
    const Mat4 mvp = mat4_multiply(projection_, model);
    glUniformMatrix4fv(u_mvp_, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(u_model_, 1, GL_FALSE, model.m);
    glUniform4f(u_color_, r, g, b, alpha);
    glUniform1i(u_textured_, texture ? 1 : 0);
    glUniform1f(u_ambient_, 0.62f);
    glUniform1f(u_flip_, flip_);
    glUniform1f(u_shine_, shine_);
    glUniform1f(u_sheen_, sheen_);
    glUniform1f(u_spin_, spin_);
    if (reflect)
        glUniform2f(u_fade_, reflect->floor_y, reflect->fade_length);
    else
        glUniform2f(u_fade_, 0.0f, 0.0f);
    if (texture)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    glDrawArrays(GL_TRIANGLES, first, count);
}

void Gl3D::draw(const CaseOp &op) noexcept
{
    if (!ready())
        return;
    const float s = op.scale;
    Mat4 place = mat4_multiply(mat4_translate(op.x, op.y, op.z),
                               mat4_multiply(mat4_rotate_z(op.roll), mat4_multiply(mat4_rotate_x(op.pitch), mat4_rotate_y(op.yaw))));
    if (op.reflection)
    {
        // Mirror about the floor line: y' = 2 floor - y. The mirror flips the winding, so cull the other side.
        const Mat4 mirror = mat4_multiply(mat4_translate(0, op.floor_y, 0),
                                          mat4_multiply(mat4_scale(1, -1, 1), mat4_translate(0, -op.floor_y, 0)));
        place = mat4_multiply(mirror, place);
        glFrontFace(GL_CW);
    }
    const float w = op.width * s, h = op.height * s, t = op.depth * s;
    const Mat4 body = mat4_multiply(place, mat4_scale(w, h, t));
    const CaseOp *fade = op.reflection ? &op : nullptr;
    const float a = op.alpha * (op.reflection ? 0.34f : 1.0f);
    const float k = op.shade;
    if (op.part != 2)
    {
        // The back wall and the sides first, then the tray inside.
        flip_ = op.flip_back ? 1.0f : 0.0f;
        if (op.back)
            draw_range(body, box_back_, 6, op.back, k, k, k, a, fade);
        else
            draw_range(body, box_back_, 6, 0, op.side_r * k, op.side_g * k, op.side_b * k, a, fade);
        flip_ = 0.0f;
        if (op.spine)
        {
            // Sides 12..17 are the left edge: replace it with the spine picture.
            draw_range(body, box_sides_ + 6, box_sides_count_ - 6, 0, op.side_r * k, op.side_g * k, op.side_b * k, a, fade);
            flip_ = 1.0f;
            draw_range(body, spine_first_, 6, op.spine, k, k, k, a, fade);
            flip_ = 0.0f;
        }
        else
            draw_range(body, box_sides_, box_sides_count_, 0, op.side_r * k, op.side_g * k, op.side_b * k, a, fade);
        if (op.opening > 0.0f)
            draw_range(body, cover_first_, cover_count_, 0, 0.045f * k, 0.05f * k, 0.065f * k, a, fade);
    }
    if (op.part != 1)
    {
        // The front is a door on the left hinge once the case opens.
        Mat4 door = body;
        if (op.opening > 0.0f)
        {
            const Mat4 hinge = mat4_multiply(mat4_translate(-0.5f, 0, 0.5f),
                                             mat4_multiply(mat4_rotate_y(-op.opening * 1.75f), mat4_translate(0.5f, 0, -0.5f)));
            door = mat4_multiply(place, mat4_multiply(mat4_scale(w, h, t), hinge));
        }
        shine_ = op.reflection ? 0.0f : op.shine;
        if (op.front)
            draw_range(door, box_front_, 6, op.front, k, k, k, a, fade);
        else
            draw_range(door, box_front_, 6, 0, op.side_r * k * 1.6f, op.side_g * k * 1.6f, op.side_b * k * 1.6f, a, fade);
    }
    shine_ = 0.0f;
    if (op.reflection)
        glFrontFace(GL_CCW);
}

void Gl3D::draw(const DiscOp &op) noexcept
{
    if (!ready() || disc_count_ == 0)
        return;
    const Mat4 place = mat4_multiply(mat4_translate(op.x, op.y, op.z), mat4_rotate_y(op.yaw));
    const Mat4 turn = mat4_multiply(place, mat4_rotate_z(op.spin));
    auto disc = [&](float radius, std::uint32_t texture, float r, float g, float b, float zoffset)
    {
        const Mat4 model = mat4_multiply(turn, mat4_multiply(mat4_translate(0, 0, zoffset), mat4_scale(radius * 2.0f, radius * 2.0f, 1.0f)));
        draw_range(model, disc_first_, disc_count_, texture, r, g, b, op.alpha, nullptr);
    };
    const float R = op.radius;
    sheen_ = op.sheen;
    spin_ = op.spin;
    disc(R, 0, op.tint_r, op.tint_g, op.tint_b, 0.0f);              // silver rim
    disc(R * 0.93f, op.label, op.label ? 1.0f : op.tint_r * 0.8f, op.label ? 1.0f : op.tint_g * 0.8f,
         op.label ? 1.0f : op.tint_b * 0.8f, 0.4f); // printed label
    sheen_ = 0.0f;
    disc(R * 0.30f, 0, op.tint_r * 0.9f, op.tint_g * 0.9f, op.tint_b * 0.92f, 0.8f); // clear hub
    disc(R * 0.12f, 0, op.hole_r, op.hole_g, op.hole_b, 1.2f);       // hole
}
} // namespace estante
