/// @file export.h
/// @brief Shared import/export annotations for GameWIP library public APIs.

#pragma once

#if defined(_WIN32)
#define GAMEWIP_SHARED_EXPORT __declspec(dllexport)
#define GAMEWIP_SHARED_IMPORT __declspec(dllimport)
#define GAMEWIP_SHARED_LOCAL
#elif defined(__GNUC__) || defined(__clang__)
#define GAMEWIP_SHARED_EXPORT __attribute__((visibility("default")))
#define GAMEWIP_SHARED_IMPORT __attribute__((visibility("default")))
#define GAMEWIP_SHARED_LOCAL __attribute__((visibility("hidden")))
#else
#define GAMEWIP_SHARED_EXPORT
#define GAMEWIP_SHARED_IMPORT
#define GAMEWIP_SHARED_LOCAL
#endif
