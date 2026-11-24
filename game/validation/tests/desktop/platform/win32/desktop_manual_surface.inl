/// @file desktop_manual_surface.inl
/// @brief Renderer-free surfaces for manual Desktop scenarios.

/// @brief Selects renderer-free artwork for a visible manual scenario.
enum class ManualSurfaceLayout : std::uint8_t
{
    Standard,
    DragDropSource,
    DragDropTarget,
    CustomChromePrimary,
    CustomChromeReplacement
};

/// @brief Paints a renderer-free test surface across the complete native client area.
/// @details GameWIPTests does not attach a renderer, so resized or fullscreen HWND backing
/// pixels are otherwise undefined and can misleadingly retain only the old windowed area.
void paintManualValidationSurface(const Desktop::Window &window, ManualSurfaceLayout layout = ManualSurfaceLayout::Standard)
{
    // Require an open window and a valid native handle.
    if(!window.isOpen())
    {
        return;
    }

    const auto native = Desktop::Native::Win32::getHandle(window);

    if(!native.status.ok() || native->window == nullptr)
    {
        return;
    }

    RECT client{};

    if(GetClientRect(native->window, &client) == FALSE)
    {
        return;
    }

    HDC device = GetDC(native->window);

    if(device == nullptr)
    {
        return;
    }

    // Prepare DPI-scaled geometry and drawing helpers.
    const UINT dpi = GetDpiForWindow(native->window);
    const auto logical = [dpi](int value)
    {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    const auto clippedRect = [&](int left, int top, int right, int bottom)
    {
        return RECT{
            std::clamp(static_cast<LONG>(logical(left)), client.left, client.right),
            std::clamp(static_cast<LONG>(logical(top)), client.top, client.bottom),
            std::clamp(static_cast<LONG>(logical(right)), client.left, client.right),
            std::clamp(static_cast<LONG>(logical(bottom)), client.top, client.bottom)};
    };
    const auto fill = [&](RECT rect, COLORREF color)
    {
        SetDCBrushColor(device, color);
        FillRect(device, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    };
    const auto drawText = [&](std::wstring_view text, RECT rect, int, COLORREF color, UINT format)
    {
        HGDIOBJ previousFont = SelectObject(device, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(device, TRANSPARENT);
        SetTextColor(device, color);
        DrawTextW(device, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);

        if(previousFont != nullptr)
        {
            SelectObject(device, previousFont);
        }
    };

    // Paint the selected manual scenario.
    fill(client, RGB(22, 70, 126));

    switch(layout)
    {
    case ManualSurfaceLayout::DragDropSource:
    {
        fill(client, RGB(25, 104, 72));
        drawText(L"GAMEWIP DRAG SOURCE", clippedRect(20, 100, 540, 155), 26, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Press and hold the requested button here, then drag to the target or foreign application.",
            clippedRect(30, 170, 530, 270),
            18,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        break;
    }
    case ManualSurfaceLayout::DragDropTarget:
    {
        const RECT preferred = clippedRect(120, 90, 480, 270);
        fill(preferred, RGB(20, 128, 94));
        drawText(L"GAMEWIP DROP TARGET", clippedRect(20, 20, 540, 70), 24, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"REGION 2 - MOVE PREFERRED", preferred, 18, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        drawText(
            L"Blue remainder: whole-client Region 1, Copy preferred",
            clippedRect(20, 285, 540, 335),
            16,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case ManualSurfaceLayout::CustomChromePrimary:
    {
        const RECT drag = clippedRect(0, 0, 760, 48);
        const RECT gap = clippedRect(760, 0, 800, 48);
        const RECT menu = clippedRect(0, 0, 48, 48);
        const RECT minimize = clippedRect(800, 0, 848, 48);
        const RECT maximize = clippedRect(848, 0, 896, 48);
        const RECT close = clippedRect(896, 0, 960, 48);
        fill(drag, RGB(15, 112, 132));
        fill(gap, RGB(55, 65, 78));
        fill(menu, RGB(12, 84, 105));
        fill(minimize, RGB(55, 65, 78));
        fill(maximize, RGB(55, 65, 78));
        fill(close, RGB(182, 45, 55));
        drawText(L"MENU", clippedRect(14, 0, 48, 48), 11, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"DRAG AREA", drag, 20, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"MIN", minimize, 12, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"MAX", maximize, 12, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"CLOSE", close, 11, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"CUSTOM CHROME TEST", clippedRect(80, 150, 880, 190), 24, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Drag the teal title region and use every visible system control.",
            clippedRect(80, 210, 880, 250),
            20,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Hover MAX for Snap Layout; resize from every cyan edge and corner.",
            clippedRect(80, 270, 880, 310),
            20,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case ManualSurfaceLayout::CustomChromeReplacement:
    {
        const RECT inactive = clippedRect(0, 0, 960, 48);
        const RECT replacement = clippedRect(0, 48, 640, 88);
        fill(inactive, RGB(80, 48, 55));
        fill(replacement, RGB(15, 132, 102));
        drawText(L"OLD TOP STRIP - INACTIVE", inactive, 18, RGB(255, 210, 210), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"REPLACEMENT DRAG STRIP (y=48..87)", replacement, 17, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Only the green replacement strip should move the Window.",
            clippedRect(100, 180, 860, 220),
            24,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"The red old strip and every former control region must be inactive.",
            clippedRect(100, 240, 860, 280),
            20,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case ManualSurfaceLayout::Standard:
    default:
    {
        const LONG middle = client.top + (client.bottom - client.top) / 2;
        const RECT title{client.left, middle - logical(38), client.right, middle - logical(4)};
        const RECT subtitle{client.left, middle + logical(4), client.right, middle + logical(38)};
        drawText(L"GameWIP manual validation surface", title, 28, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"The blue surface and cyan inset border must cover the complete client area.",
            subtitle,
            22,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    }

    // Draw a shared inset border and release the native drawing resources.
    const int inset = std::max(4, MulDiv(10, static_cast<int>(dpi), 96));
    const int penWidth = std::max(2, MulDiv(6, static_cast<int>(dpi), 96));
    HPEN edgePen = CreatePen(PS_SOLID, penWidth, RGB(70, 210, 255));
    HGDIOBJ previousPen = edgePen != nullptr ? SelectObject(device, edgePen) : nullptr;
    HGDIOBJ previousBrush = SelectObject(device, GetStockObject(NULL_BRUSH));
    Rectangle(device, client.left + inset, client.top + inset, client.right - inset, client.bottom - inset);
    SelectObject(device, previousBrush);

    if(previousPen != nullptr)
    {
        SelectObject(device, previousPen);
    }

    if(edgePen != nullptr)
    {
        DeleteObject(edgePen);
    }

    ReleaseDC(native->window, device);
}

template <typename Payload> [[nodiscard]] bool consumeEventOfType(Desktop::Window &window)
{
    bool found = false;
    Desktop::Types::Event event;

    while(window.popEvent(event))
    {
        found = found || event.getIf<Payload>() != nullptr;
    }

    return found;
}
