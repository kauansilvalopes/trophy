// Estante - GPU frame loop. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gl_canvas.hpp"

namespace estante
{
using GlDrawScene = void (*)(GlCanvas &) noexcept;

// Opens the display and runs one frame per vertical blank forever: draw, render, swap.
[[noreturn]] void run_gl(GlDrawScene draw) noexcept;
} // namespace estante
