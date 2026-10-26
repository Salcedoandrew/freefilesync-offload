// *****************************************************************************
// * macOS port shim for the FreeFileSync tree (not part of upstream).          *
// * Force-included before everything else by Makefile.macos:                   *
// *   -include zen/mac_compat.h -include zen/i18n.h                            *
// * Must come BEFORE zen/i18n.h, which pulls in scope_guard.h et al.           *
// *****************************************************************************
#pragma once

// libstdc++ pulls these in transitively; libc++ does not.
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <exception>
#include <type_traits>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

// glibc exposes sub-second times as st_mtim/st_atim/st_ctim; Darwin uses *timespec.
#define st_mtim st_mtimespec
#define st_atim st_atimespec
#define st_ctim st_ctimespec
