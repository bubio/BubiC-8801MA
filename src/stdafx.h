// stdafx.h : Correctly ordered include file for precompiled headers.
// This file establishes a sane and consistent include order for the entire project
// to resolve MSVC-specific header conflicts.

#pragma once

#ifndef STDAFX_H
#define STDAFX_H

// --- 0. Pre-processor Defines for Libraries ---
// ImGui: Enable math operators for ImVec2, ImVec4, etc.
#define IMGUI_DEFINE_MATH_OPERATORS

// Windows: Define early to avoid redefinition warnings and to set the API level.
#define STRICT

// --- 1. Low-level Platform-Specific Headers ---
// windows.h must be included before many C++ standard library headers to avoid conflicts.
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

// --- 2. Standard C/C++ Library Headers ---
// Now that windows.h is included, we can safely include standard libraries.
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// --- 3. Third-Party Library Headers ---
#include <SDL3/SDL.h>
#include "imgui.h"

// --- 4. Project's Own Common Header ---
// This header relies on the headers above being included first.
// Its own internal includes will now be safely ignored due to include guards.
#include "common.h"

#endif //STDAFX_H
