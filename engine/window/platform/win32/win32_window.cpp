#include "window/window.h"
#include "window/internal/window_message_access.h"
#include "input/platform/win32/win32_input.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "window/platform/win32/internal/win32_window_helpers.inl"
#include "window/platform/win32/internal/win32_window_state.inl"
#include "window/platform/win32/internal/win32_window_message_access.inl"
#include "window/platform/win32/internal/win32_window_lifecycle.inl"
#include "window/platform/win32/internal/win32_window_cursor.inl"
#include "window/platform/win32/internal/win32_window_modes.inl"
#include "window/platform/win32/internal/win32_window_cursor_helpers.inl"
#include "window/platform/win32/internal/win32_window_monitor.inl"
