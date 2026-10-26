// Trophy Manager - GPU frame loop (shared with Estante). SPDX-License-Identifier: GPL-3.0-or-later
#include "gl_main.hpp"

#include "core/save_file.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/system.hpp"

#include <GL/glcorearb.h>

#include <cstdint>
#include <string>

namespace estante
{
namespace
{
constexpr const char *font_path = "/app0/assets/fonts/inter.huifont";
const char *failure_reason = "";

// Last resort when the GPU path cannot start: a still message drawn on the CPU, so the player sees why
// instead of a black screen. The same text goes to the log (/download0/hui/dev/app.log, klog).
void draw_failure(ps5::demo::Canvas &c) noexcept
{
    c.clear(ps5::demo::Color::background);
    c.text(80, 90, "TROPHY MANAGER - GPU START FAILED", 5, ps5::demo::Color::white);
    c.text(80, 190, failure_reason, 3, ps5::demo::Color::yellow);
    c.text(80, 260, "CLOSE THE APP WITH THE PS BUTTON AND SEND", 3, ps5::demo::Color::white);
    c.text(80, 300, "/DOWNLOAD0/HUI/DEV/APP.LOG TO THE DEVELOPER", 3, ps5::demo::Color::white);
}

[[noreturn]] void fail(const char *reason) noexcept
{
    hui::sys::log("[TROPHY] gl: %s", reason);
    failure_reason = reason;
    ps5::demo::run(draw_failure, "estante gpu failure");
}
} // namespace

void run_gl(GlDrawScene draw) noexcept
{
    static hui::ps5::Display display;
    static hui::gfx::Renderer renderer;
    static hui::gfx::Font font;
    static hui::gfx::DrawList list;
    static GlCanvas canvas;

    hui::sys::log("[TROPHY] gl: opening display");
    if (!display.open(1920, 1080))
    {
        hui::sys::log("[TROPHY] gl: display open failed (egl error %s)",
                      hui::ps5::egl_error_name(display.last_error()));
        fail("DISPLAY OPEN FAILED");
    }
    if (!renderer.init())
        fail("RENDERER INIT FAILED");
    std::string bytes;
    if (!hui::save::read_file(font_path, &bytes) || !font.load(bytes))
    {
        hui::sys::log("[TROPHY] gl: font %s failed: %s", font_path, font.error().c_str());
        fail("FONT LOAD FAILED");
    }
    const std::uint32_t font_texture = renderer.batch().create_font_texture(font);
    hui::sys::log("[TROPHY] gl: ready %dx%d", display.width(), display.height());

    std::uint64_t frames = 0;
    std::int64_t last_report = hui::sys::monotonic_us();
    for (;;)
    {
        canvas.begin(&renderer.batch(), &font, font_texture);
        draw(canvas);
        renderer.begin();
        canvas.submit(renderer);
        renderer.present(0, display.width(), display.height());
        if (!display.swap())
            fail("DISPLAY SWAP FAILED");
        frames++;
        const std::int64_t now = hui::sys::monotonic_us();
        if (now - last_report >= 5000000)
        {
            hui::sys::log("[TROPHY] gl: frames=%llu fps=%.1f draw_calls=%zu",
                          static_cast<unsigned long long>(frames),
                          static_cast<double>(frames) * 1e6 / static_cast<double>(now - last_report),
                          renderer.last_draw_calls());
            frames = 0;
            last_report = now;
        }
    }
}
} // namespace estante
