#pragma once

#include "contour_engine.hpp"

#include <string>

namespace scm {

bool loadRgbaWithWic(const std::wstring& path, RgbaImage& image, std::wstring& error);

} // namespace scm
