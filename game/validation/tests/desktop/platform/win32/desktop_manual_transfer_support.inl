/// @file desktop_manual_transfer_support.inl
/// @brief Manual data-transfer sample helpers.

inline constexpr std::string_view kManualInteropText =
    "GameWIP strict Unicode: caf\xC3\xA9 | \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E | \xF0\x9F\x98\x80";

[[nodiscard]] Desktop::Types::DataTransfer::Image manualInteropImage()
{
    Desktop::Types::DataTransfer::Image image{{128, 128}, std::vector<std::byte>(std::size_t{128} * 128 * 4)};

    for(std::size_t y = 0; y < 128; ++y)
    {
        for(std::size_t x = 0; x < 128; ++x)
        {
            const std::size_t offset = (y * 128 + x) * 4;
            const bool left = x < 64;
            const bool top = y < 64;
            image.rgba8[offset] = std::byte{static_cast<unsigned char>((top && left) || (!top && !left) ? 255 : 0)};
            image.rgba8[offset + 1] = std::byte{static_cast<unsigned char>(!left ? 255 : 0)};
            image.rgba8[offset + 2] = std::byte{static_cast<unsigned char>(!top ? 255 : 0)};
            image.rgba8[offset + 3] = std::byte{255};
        }
    }

    return image;
}

[[nodiscard]] GameWIP::Status::Types::Status writeManualInteropBitmap(
    const std::filesystem::path &path,
    const Desktop::Types::DataTransfer::Image &image)
{
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(image.rgba8.size());
    BITMAPINFOHEADER bitmap{};
    bitmap.biSize = sizeof(bitmap);
    bitmap.biWidth = static_cast<LONG>(image.size.width);
    bitmap.biHeight = -static_cast<LONG>(image.size.height);
    bitmap.biPlanes = 1;
    bitmap.biBitCount = 32;
    bitmap.biCompression = BI_RGB;
    std::vector<std::byte> bytes(file.bfSize);
    std::ranges::copy(std::as_bytes(std::span{&file, 1}), bytes.begin());
    std::ranges::copy(std::as_bytes(std::span{&bitmap, 1}), bytes.begin() + sizeof(file));

    for(std::size_t offset = 0; offset < image.rgba8.size(); offset += 4)
    {
        const std::size_t destination = file.bfOffBits + offset;
        bytes[destination] = image.rgba8[offset + 2];
        bytes[destination + 1] = image.rgba8[offset + 1];
        bytes[destination + 2] = image.rgba8[offset];
        bytes[destination + 3] = image.rgba8[offset + 3];
    }

    return GameWIP::FileSystem::writeAllBytes(path, bytes).status();
}

/// @brief Shows channel order, row order, and alpha against a checkerboard in the target.
void paintManualImagePreview(const Desktop::Window &window, const Desktop::Types::DataTransfer::Image &image)
{
    if(image.size.width == 0 || image.size.height == 0 || image.rgba8.empty())
    {
        return;
    }

    const auto native = Desktop::Native::Win32::getHandle(window);
    if(!native.ok())
    {
        return;
    }

    constexpr std::size_t extent = 128;
    std::array<std::byte, extent * extent * 4> preview{};

    for(std::size_t y = 0; y < extent; ++y)
    {
        for(std::size_t x = 0; x < extent; ++x)
        {
            const std::size_t source = ((y * image.size.height / extent) * image.size.width + x * image.size.width / extent) * 4;

            if(source + 3 >= image.rgba8.size())
            {
                return;
            }
            const unsigned int alpha = std::to_integer<unsigned int>(image.rgba8[source + 3]);
            const unsigned int background = ((x / 8 + y / 8) % 2) == 0 ? 192 : 240;

            for(std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto color = std::to_integer<unsigned int>(image.rgba8[source + 2 - channel]);
                preview[(y * extent + x) * 4 + channel] = std::byte{static_cast<unsigned char>((color * alpha + background * (255 - alpha)) / 255)};
            }
        }
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = 128;
    bitmap.bmiHeader.biHeight = -128;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    const HDC dc = GetDC(native.value().window);

    if(dc == nullptr)
    {
        return;
    }

    StretchDIBits(dc, 18, 170, 128, 128, 0, 0, 128, 128, preview.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(native.value().window, dc);
}
