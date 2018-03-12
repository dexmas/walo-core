#pragma once

#if defined(_MSC_VER)
#define  WALO_COMPILER_MSVC
#elif defined(__GNUC__)
#define  WALO_COMPILER_GCC
#endif

#if defined(_WIN32) || defined(_WIN64)
#define WALO_PLATFORM_WINDOWS
#endif