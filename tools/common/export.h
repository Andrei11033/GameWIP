/// @file export.h
/// @brief Shared import/export annotations for library public APIs.

#pragma once

#if defined(_WIN32)
#define INTERNAL_TOOLS_SHARED_EXPORT __declspec(dllexport)
#define INTERNAL_TOOLS_SHARED_IMPORT __declspec(dllimport)
#define INTERNAL_TOOLS_SHARED_LOCAL
#elif defined(__GNUC__) || defined(__clang__)
#define INTERNAL_TOOLS_SHARED_EXPORT __attribute__((visibility("default")))
#define INTERNAL_TOOLS_SHARED_IMPORT __attribute__((visibility("default")))
#define INTERNAL_TOOLS_SHARED_LOCAL __attribute__((visibility("hidden")))
#else
#define INTERNAL_TOOLS_SHARED_EXPORT
#define INTERNAL_TOOLS_SHARED_IMPORT
#define INTERNAL_TOOLS_SHARED_LOCAL
#endif
