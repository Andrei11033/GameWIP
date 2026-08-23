#include "win32_input.h"
#include "base/platform/win32/dynamic_library.h"
#include "input/internal/input_state_access.h"

#include <windows.h>
#include <windowsx.h>
#include <hidsdi.h>
#include <hidusage.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <xinput.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER
#define HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER 0x08
#endif

namespace
{
    namespace InputInternal = GameWIP::Input::Internal;
    namespace KeyboardControlCode = GameWIP::Input::KeyboardControlCode;
    using GameWIP::Input::ControlCode;
    using GameWIP::Input::DeviceIndex;
    using GameWIP::Input::GamepadAxis;
    using GameWIP::Input::GamepadButton;
    using GameWIP::Input::InputControl;
    using GameWIP::Input::InputControlInfo;
    using GameWIP::Input::InputControlType;
    using GameWIP::Input::InputDeviceBackend;
    using GameWIP::Input::InputDeviceInfo;
    using GameWIP::Input::InputDeviceRef;
    using GameWIP::Input::InputDeviceRegistry;
    using GameWIP::Input::InputDeviceType;
    using GameWIP::Input::InputState;
    using GameWIP::Input::makeDeviceAxis;
    using GameWIP::Input::makeDeviceButton;
    using GameWIP::Input::makeGamepadAxis;
    using GameWIP::Input::makeGamepadButton;
    using GameWIP::Input::makeKeyboardKey;
    using GameWIP::Input::makeMouseButton;
    using GameWIP::Input::makeMouseWheel;
    using GameWIP::Input::MouseButton;
    using GameWIP::Input::MouseWheel;

    constexpr DWORD maxGamepadCount = 4;
    constexpr auto disconnectedGamepadPollInterval = std::chrono::seconds(1);

    using XInputGetStateFn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);

    XInputGetStateFn cachedXInputGetState = nullptr;
    bool attemptedXInputLoad = false;
    std::array<DWORD, maxGamepadCount> cachedGamepadPacketNumbers{};
    std::array<std::uint64_t, maxGamepadCount> cachedGamepadClearGenerations{};
    std::array<const InputState *, maxGamepadCount> cachedGamepadInputStates{};
    std::array<DeviceIndex, maxGamepadCount> cachedGamepadDeviceIndices{};
    std::array<bool, maxGamepadCount> cachedGamepadConnected{};
    std::array<bool, maxGamepadCount> hasCachedGamepadPacket{};
    std::array<bool, maxGamepadCount> gamepadControlsCleared{true, true, true, true};
    std::chrono::steady_clock::time_point nextDisconnectedGamepadPollTime{};
    DWORD nextDisconnectedGamepadSlotToPoll = 0;
    bool initialXInputScanComplete = false;

    constexpr std::array<GamepadButton, 15> allGamepadButtons{
        GamepadButton::North,
        GamepadButton::South,
        GamepadButton::East,
        GamepadButton::West,
        GamepadButton::DpadUp,
        GamepadButton::DpadDown,
        GamepadButton::DpadLeft,
        GamepadButton::DpadRight,
        GamepadButton::LeftShoulder,
        GamepadButton::RightShoulder,
        GamepadButton::Back,
        GamepadButton::Start,
        GamepadButton::Guide,
        GamepadButton::LeftStick,
        GamepadButton::RightStick};

    constexpr std::array<GamepadAxis, 6> allGamepadAxes{
        GamepadAxis::LeftX,
        GamepadAxis::LeftY,
        GamepadAxis::RightX,
        GamepadAxis::RightY,
        GamepadAxis::LeftTrigger,
        GamepadAxis::RightTrigger};

    constexpr ControlCode hidButtonCodeBase = 0x00010000;
    constexpr ControlCode hidAxisCodeBase = 0x00020000;
    constexpr ControlCode hidHatCodeBase = 0x00030000;
    constexpr std::uint64_t fnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t fnvPrime = 1099511628211ull;

    struct HidButtonRuntime
    {
        USAGE usagePage = 0;
        USAGE usageMin = 0;
        USAGE usageMax = 0;
        USHORT linkCollection = 0;
        std::vector<InputControl> controls;
    };

    struct HidValueRuntime
    {
        USAGE usagePage = 0;
        USAGE usage = 0;
        USHORT linkCollection = 0;
        LONG logicalMinimum = 0;
        LONG logicalMaximum = 0;
        InputControl control{};
        bool isHat = false;
        std::array<InputControl, 4> hatButtons{};
        LONG previousRawValue = std::numeric_limits<LONG>::min();
    };

    struct HidDeviceRuntime
    {
        HANDLE rawDevice = nullptr;
        InputDeviceRef device{};
        InputDeviceType deviceType = InputDeviceType::Gamepad;
        std::wstring nativePath;
        std::string setupDeviceId;
        std::vector<std::string> hardwareIds;
        std::vector<unsigned char> preparsedData;
        HIDP_CAPS caps{};
        std::vector<HidButtonRuntime> buttons;
        std::vector<HidValueRuntime> values;
        std::uint16_t vendorId = 0;
        std::uint16_t productId = 0;
        bool xInputCompatibleHid = false;
        bool usable = false;
    };

    struct XInputHidIdentity
    {
        std::wstring nativePath;
        std::string nativeIdentity;
        std::uint64_t nativeIdentityHash = 0;
        std::string setupDeviceId;
        std::uint16_t vendorId = 0;
        std::uint16_t productId = 0;
    };

    std::vector<HidDeviceRuntime> hidDevices;
    std::vector<XInputHidIdentity> xInputHidIdentities;

    PHIDP_PREPARSED_DATA getPreparsedData(HidDeviceRuntime &device)
    {
        return reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsedData.data());
    }

    std::uint64_t hashWideIdentity(std::wstring_view text)
    {
        std::uint64_t hash = fnvOffset;
        for (wchar_t character : text)
        {
            const wchar_t folded = static_cast<wchar_t>(std::towlower(character));
            hash ^= static_cast<std::uint64_t>(folded & 0xFF);
            hash *= fnvPrime;
            hash ^= static_cast<std::uint64_t>((folded >> 8) & 0xFF);
            hash *= fnvPrime;
        }

        return hash == 0 ? 1 : hash;
    }

    std::string wideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (requiredSize <= 0)
        {
            return {};
        }

        std::string output(static_cast<std::size_t>(requiredSize), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), requiredSize, nullptr, nullptr);
        return output;
    }

    std::wstring toLowerWide(std::wstring_view text)
    {
        std::wstring output;
        output.reserve(text.size());
        for (wchar_t character : text)
        {
            output.push_back(static_cast<wchar_t>(std::towlower(character)));
        }

        return output;
    }

    std::string toLowerAscii(std::string_view text)
    {
        std::string output;
        output.reserve(text.size());
        for (char character : text)
        {
            if (character >= 'A' && character <= 'Z')
            {
                output.push_back(static_cast<char>(character - 'A' + 'a'));
            }
            else
            {
                output.push_back(character);
            }
        }

        return output;
    }

    bool containsMarker(std::string_view text, std::string_view marker)
    {
        return toLowerAscii(text).find(toLowerAscii(marker)) != std::string::npos;
    }

    bool containsMarker(std::wstring_view text, std::wstring_view marker)
    {
        return toLowerWide(text).find(toLowerWide(marker)) != std::wstring::npos;
    }

    bool tryParseHexAfterMarker(std::wstring_view text, std::wstring_view marker, std::uint16_t &outValue)
    {
        const std::size_t markerPosition = text.find(marker);
        if (markerPosition == std::wstring_view::npos || markerPosition + marker.size() + 4 > text.size())
        {
            return false;
        }

        std::uint16_t value = 0;
        for (std::size_t digitIndex = 0; digitIndex < 4; ++digitIndex)
        {
            const wchar_t digit = text[markerPosition + marker.size() + digitIndex];
            value <<= 4;
            if (digit >= L'0' && digit <= L'9')
            {
                value |= static_cast<std::uint16_t>(digit - L'0');
            }
            else if (digit >= L'a' && digit <= L'f')
            {
                value |= static_cast<std::uint16_t>(10 + digit - L'a');
            }
            else if (digit >= L'A' && digit <= L'F')
            {
                value |= static_cast<std::uint16_t>(10 + digit - L'A');
            }
            else
            {
                return false;
            }
        }

        outValue = value;
        return true;
    }

    HANDLE openHidDevicePath(const std::wstring &path)
    {
        return CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    std::string getHidProductName(const std::wstring &path)
    {
        HANDLE deviceHandle = openHidDevicePath(path);
        if (deviceHandle == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        wchar_t productName[128]{};
        const bool ok = HidD_GetProductString(deviceHandle, productName, sizeof(productName)) != FALSE;
        CloseHandle(deviceHandle);
        if (!ok || productName[0] == L'\0')
        {
            return {};
        }

        return wideToUtf8(productName);
    }

    void getHidAttributes(const std::wstring &path, std::uint16_t &outVendorId, std::uint16_t &outProductId)
    {
        outVendorId = 0;
        outProductId = 0;

        HANDLE deviceHandle = openHidDevicePath(path);
        if (deviceHandle != INVALID_HANDLE_VALUE)
        {
            HIDD_ATTRIBUTES attributes{};
            attributes.Size = sizeof(attributes);
            if (HidD_GetAttributes(deviceHandle, &attributes) != FALSE)
            {
                outVendorId = attributes.VendorID;
                outProductId = attributes.ProductID;
            }
            CloseHandle(deviceHandle);
        }

        if (outVendorId == 0)
        {
            const std::wstring lowerPath = toLowerWide(path);
            tryParseHexAfterMarker(lowerPath, L"vid_", outVendorId);
        }

        if (outProductId == 0)
        {
            const std::wstring lowerPath = toLowerWide(path);
            tryParseHexAfterMarker(lowerPath, L"pid_", outProductId);
        }
    }

    std::vector<std::string> readHardwareIds(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA &deviceInfoData)
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, nullptr, nullptr, 0, &requiredSize);
        if (requiredSize == 0)
        {
            return {};
        }

        std::vector<wchar_t> buffer((requiredSize / sizeof(wchar_t)) + 1, L'\0');
        if (SetupDiGetDeviceRegistryPropertyW(
                deviceInfoSet,
                &deviceInfoData,
                SPDRP_HARDWAREID,
                nullptr,
                reinterpret_cast<PBYTE>(buffer.data()),
                requiredSize,
                nullptr) == FALSE)
        {
            return {};
        }

        std::vector<std::string> hardwareIds;
        for (const wchar_t *entry = buffer.data(); entry != nullptr && *entry != L'\0'; entry += std::wcslen(entry) + 1)
        {
            hardwareIds.push_back(wideToUtf8(entry));
        }

        return hardwareIds;
    }

    std::string readDeviceInstanceId(SP_DEVINFO_DATA &deviceInfoData)
    {
        ULONG requiredSize = 0;
        CONFIGRET sizeResult = CM_Get_Device_ID_Size(&requiredSize, deviceInfoData.DevInst, 0);
        if (sizeResult != CR_SUCCESS || requiredSize == 0)
        {
            return {};
        }

        std::vector<wchar_t> buffer(static_cast<std::size_t>(requiredSize) + 1, L'\0');
        CONFIGRET idResult = CM_Get_Device_IDW(deviceInfoData.DevInst, buffer.data(), requiredSize + 1, 0);
        return idResult == CR_SUCCESS ? wideToUtf8(buffer.data()) : std::string{};
    }

    void enrichHidSetupMetadata(HidDeviceRuntime &runtime)
    {
        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
        if (deviceInfoSet == INVALID_HANDLE_VALUE)
        {
            return;
        }

        const std::wstring targetPath = toLowerWide(runtime.nativePath);
        for (DWORD index = 0;; ++index)
        {
            SP_DEVICE_INTERFACE_DATA interfaceData{};
            interfaceData.cbSize = sizeof(interfaceData);
            if (SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid, index, &interfaceData) == FALSE)
            {
                break;
            }

            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);
            if (requiredSize == 0)
            {
                continue;
            }

            std::vector<unsigned char> detailBuffer(requiredSize);
            auto detailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            SP_DEVINFO_DATA deviceInfoData{};
            deviceInfoData.cbSize = sizeof(deviceInfoData);
            if (SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData, requiredSize, nullptr, &deviceInfoData) == FALSE)
            {
                continue;
            }

            if (toLowerWide(detailData->DevicePath) != targetPath)
            {
                continue;
            }

            runtime.hardwareIds = readHardwareIds(deviceInfoSet, deviceInfoData);
            runtime.setupDeviceId = readDeviceInstanceId(deviceInfoData);
            break;
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }

    bool isXInputHidRuntime(const HidDeviceRuntime &runtime)
    {
        if (containsMarker(runtime.nativePath, L"ig_"))
        {
            return true;
        }

        for (const std::string &hardwareId : runtime.hardwareIds)
        {
            if (containsMarker(hardwareId, "ig_"))
            {
                return true;
            }
        }

        return false;
    }

    bool isSupportedHidUsage(USHORT usagePage, USHORT usage)
    {
        return usagePage == HID_USAGE_PAGE_GENERIC &&
               (usage == HID_USAGE_GENERIC_GAMEPAD || usage == HID_USAGE_GENERIC_JOYSTICK || usage == HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER);
    }

    ControlCode makeHidButtonCode(USAGE usage)
    {
        switch (usage)
        {
        case 1:
            return static_cast<ControlCode>(GamepadButton::South);
        case 2:
            return static_cast<ControlCode>(GamepadButton::East);
        case 3:
            return static_cast<ControlCode>(GamepadButton::West);
        case 4:
            return static_cast<ControlCode>(GamepadButton::North);
        case 5:
            return static_cast<ControlCode>(GamepadButton::LeftShoulder);
        case 6:
            return static_cast<ControlCode>(GamepadButton::RightShoulder);
        case 7:
            return static_cast<ControlCode>(GamepadButton::Back);
        case 8:
            return static_cast<ControlCode>(GamepadButton::Start);
        case 9:
            return static_cast<ControlCode>(GamepadButton::LeftStick);
        case 10:
            return static_cast<ControlCode>(GamepadButton::RightStick);
        default:
            return hidButtonCodeBase + static_cast<ControlCode>(usage);
        }
    }

    ControlCode makeHidAxisCode(InputDeviceType deviceType, USAGE usage)
    {
        if (deviceType == InputDeviceType::Gamepad)
        {
            switch (usage)
            {
            case HID_USAGE_GENERIC_X:
                return static_cast<ControlCode>(GamepadAxis::LeftX);
            case HID_USAGE_GENERIC_Y:
                return static_cast<ControlCode>(GamepadAxis::LeftY);
            case HID_USAGE_GENERIC_Z:
                return static_cast<ControlCode>(GamepadAxis::RightX);
            case HID_USAGE_GENERIC_RZ:
                return static_cast<ControlCode>(GamepadAxis::RightY);
            case HID_USAGE_GENERIC_RX:
                return static_cast<ControlCode>(GamepadAxis::LeftTrigger);
            case HID_USAGE_GENERIC_RY:
                return static_cast<ControlCode>(GamepadAxis::RightTrigger);
            default:
                break;
            }
        }

        return hidAxisCodeBase + static_cast<ControlCode>(usage);
    }

    std::string makeHidButtonName(USAGE usage)
    {
        return "Button " + std::to_string(static_cast<unsigned int>(usage));
    }

    std::string makeHidAxisName(InputDeviceType deviceType, USAGE usage)
    {
        if (deviceType == InputDeviceType::Gamepad)
        {
            switch (usage)
            {
            case HID_USAGE_GENERIC_X:
                return "Left Stick X";
            case HID_USAGE_GENERIC_Y:
                return "Left Stick Y";
            case HID_USAGE_GENERIC_Z:
                return "Right Stick X";
            case HID_USAGE_GENERIC_RZ:
                return "Right Stick Y";
            case HID_USAGE_GENERIC_RX:
                return "Left Trigger";
            case HID_USAGE_GENERIC_RY:
                return "Right Trigger";
            default:
                break;
            }
        }

        switch (usage)
        {
        case HID_USAGE_GENERIC_X:
            return "X Axis";
        case HID_USAGE_GENERIC_Y:
            return "Y Axis";
        case HID_USAGE_GENERIC_Z:
            return "Z Axis";
        case HID_USAGE_GENERIC_RX:
            return "Rx Axis";
        case HID_USAGE_GENERIC_RY:
            return "Ry Axis";
        case HID_USAGE_GENERIC_RZ:
            return "Rz Axis";
        case HID_USAGE_GENERIC_SLIDER:
            return "Slider";
        case HID_USAGE_GENERIC_DIAL:
            return "Dial";
        case HID_USAGE_GENERIC_WHEEL:
            return "Wheel";
        default:
            return "Axis " + std::to_string(static_cast<unsigned int>(usage));
        }
    }

    bool isCenteredHidAxis(InputDeviceType deviceType, USAGE usage)
    {
        if (deviceType != InputDeviceType::Gamepad)
        {
            return false;
        }

        return usage == HID_USAGE_GENERIC_X || usage == HID_USAGE_GENERIC_Y || usage == HID_USAGE_GENERIC_Z || usage == HID_USAGE_GENERIC_RZ;
    }

    bool isInvertedCenteredHidAxis(InputDeviceType deviceType, USAGE usage)
    {
        return deviceType == InputDeviceType::Gamepad && (usage == HID_USAGE_GENERIC_Y || usage == HID_USAGE_GENERIC_RZ);
    }

    float normalizeHidValue(LONG value, LONG logicalMinimum, LONG logicalMaximum, InputDeviceType deviceType, USAGE usage)
    {
        if (logicalMaximum <= logicalMinimum)
        {
            return 0.0f;
        }

        const float normalized = static_cast<float>(value - logicalMinimum) / static_cast<float>(logicalMaximum - logicalMinimum);
        if (logicalMinimum >= 0 && !isCenteredHidAxis(deviceType, usage))
        {
            return std::clamp(normalized, 0.0f, 1.0f);
        }

        float centeredValue = std::clamp((normalized * 2.0f) - 1.0f, -1.0f, 1.0f);
        if (isInvertedCenteredHidAxis(deviceType, usage))
        {
            centeredValue = -centeredValue;
        }

        return centeredValue;
    }

    void addControlInfo(
        std::vector<InputControlInfo> &controls,
        InputControl control,
        std::string displayName,
        float minimumValue,
        float maximumValue)
    {
        if (std::any_of(
                controls.begin(),
                controls.end(),
                [control](const InputControlInfo &candidate)
                {
                    return candidate.control == control;
                }))
        {
            return;
        }

        controls.push_back(
            InputControlInfo{
                .control = control,
                .displayName = std::move(displayName),
                .minimumValue = minimumValue,
                .maximumValue = maximumValue,
                .relative = false});
    }

    bool loadHidPreparsedData(HidDeviceRuntime &runtime)
    {
        UINT preparsedSize = 0;
        if (GetRawInputDeviceInfoW(runtime.rawDevice, RIDI_PREPARSEDDATA, nullptr, &preparsedSize) != 0 || preparsedSize == 0)
        {
            return false;
        }

        runtime.preparsedData.resize(preparsedSize);
        UINT expectedSize = preparsedSize;
        const UINT result = GetRawInputDeviceInfoW(runtime.rawDevice, RIDI_PREPARSEDDATA, runtime.preparsedData.data(), &preparsedSize);
        if (result == static_cast<UINT>(-1) || result != expectedSize)
        {
            return false;
        }

        return HidP_GetCaps(getPreparsedData(runtime), &runtime.caps) == HIDP_STATUS_SUCCESS;
    }

    void buildHidButtonRuntime(HidDeviceRuntime &runtime, const HIDP_BUTTON_CAPS &cap, std::vector<InputControlInfo> &controls)
    {
        HidButtonRuntime buttonRuntime{};
        buttonRuntime.usagePage = cap.UsagePage;
        buttonRuntime.linkCollection = cap.LinkCollection;

        const USAGE usageMin = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;
        const USAGE usageMax = cap.IsRange ? cap.Range.UsageMax : cap.NotRange.Usage;
        buttonRuntime.usageMin = usageMin;
        buttonRuntime.usageMax = usageMax;

        for (USAGE usage = usageMin; usage <= usageMax; ++usage)
        {
            InputControl control = makeDeviceButton(runtime.device, makeHidButtonCode(usage));
            buttonRuntime.controls.push_back(control);
            addControlInfo(controls, control, makeHidButtonName(usage), 0.0f, 1.0f);
        }

        runtime.buttons.push_back(std::move(buttonRuntime));
    }

    void buildHatControls(HidValueRuntime &valueRuntime, std::vector<InputControlInfo> &controls)
    {
        valueRuntime.isHat = true;
        const InputDeviceRef device{valueRuntime.control.deviceType, valueRuntime.control.deviceIndex};
        valueRuntime.hatButtons = {
            makeDeviceButton(device, static_cast<ControlCode>(GamepadButton::DpadUp)),
            makeDeviceButton(device, static_cast<ControlCode>(GamepadButton::DpadRight)),
            makeDeviceButton(device, static_cast<ControlCode>(GamepadButton::DpadDown)),
            makeDeviceButton(device, static_cast<ControlCode>(GamepadButton::DpadLeft))};

        addControlInfo(controls, valueRuntime.hatButtons[0], "D-Pad Up", 0.0f, 1.0f);
        addControlInfo(controls, valueRuntime.hatButtons[1], "D-Pad Right", 0.0f, 1.0f);
        addControlInfo(controls, valueRuntime.hatButtons[2], "D-Pad Down", 0.0f, 1.0f);
        addControlInfo(controls, valueRuntime.hatButtons[3], "D-Pad Left", 0.0f, 1.0f);
    }

    void buildHidValueRuntime(HidDeviceRuntime &runtime, const HIDP_VALUE_CAPS &cap, USAGE usage, std::vector<InputControlInfo> &controls)
    {
        HidValueRuntime valueRuntime{};
        valueRuntime.usagePage = cap.UsagePage;
        valueRuntime.usage = usage;
        valueRuntime.linkCollection = cap.LinkCollection;
        valueRuntime.logicalMinimum = cap.LogicalMin;
        valueRuntime.logicalMaximum = cap.LogicalMax;
        valueRuntime.control = makeDeviceAxis(runtime.device, makeHidAxisCode(runtime.deviceType, usage));

        if (cap.UsagePage == HID_USAGE_PAGE_GENERIC && usage == HID_USAGE_GENERIC_HATSWITCH)
        {
            valueRuntime.control = makeDeviceButton(runtime.device, hidHatCodeBase + static_cast<ControlCode>(usage));
            buildHatControls(valueRuntime, controls);
        }
        else
        {
            const bool centeredAxis = cap.LogicalMin < 0 || isCenteredHidAxis(runtime.deviceType, usage);
            addControlInfo(controls, valueRuntime.control, makeHidAxisName(runtime.deviceType, usage), centeredAxis ? -1.0f : 0.0f, 1.0f);
        }

        runtime.values.push_back(valueRuntime);
    }

    void debugInputBackendMessage(std::string_view message)
    {
        std::string output = "[GameWIP][Input] ";
        output.append(message);
        output.push_back('\n');
        OutputDebugStringA(output.c_str());
    }

    bool isSameBackendIdentity(std::uint64_t left, std::uint64_t right)
    {
        return left != 0 && right != 0 && left == right;
    }

    bool findBackendMergeTarget(const InputDeviceRegistry &devices, const InputDeviceInfo &backendInfo, InputDeviceRef &outDevice)
    {
        bool found = false;
        bool ambiguous = false;
        InputDeviceRef candidate{};

        for (const InputDeviceInfo &device : devices.getDevices())
        {
            if (device.deviceType != InputDeviceType::Gamepad || !device.canonical)
            {
                continue;
            }

            bool matches = false;
            if (backendInfo.backend == InputDeviceBackend::XInput && device.hasHidFeed)
            {
                matches = isSameBackendIdentity(device.hidNativeIdentityHash, backendInfo.xInputNativeIdentityHash);
            }
            else if (backendInfo.backend == InputDeviceBackend::RawInputHID && device.hasXInputFeed)
            {
                matches = isSameBackendIdentity(device.xInputNativeIdentityHash, backendInfo.hidNativeIdentityHash);
            }

            if (!matches)
            {
                continue;
            }

            if (found)
            {
                ambiguous = true;
                break;
            }

            found = true;
            candidate = device.device;
        }

        if (ambiguous)
        {
            debugInputBackendMessage("Ambiguous HID/XInput merge candidate; keeping devices separate.");
            return false;
        }

        if (!found)
        {
            return false;
        }

        outDevice = candidate;
        return true;
    }

    const XInputHidIdentity *findXInputHidIdentityForSlot(DWORD userIndex)
    {
        // Raw HID enumeration can tell us that an XInput-compatible HID interface exists,
        // but it does not reliably tell us which XInput user index owns that interface.
        // Do not merge by count or order; a weak merge can suppress the real HID feed.
        (void)userIndex;
        return nullptr;
    }

    bool buildHidRuntime(HidDeviceRuntime &runtime, InputDeviceRegistry &devices)
    {
        if (!loadHidPreparsedData(runtime))
        {
            return false;
        }

        std::vector<InputControlInfo> controls;
        controls.reserve(32);

        getHidAttributes(runtime.nativePath, runtime.vendorId, runtime.productId);

        std::string productName = getHidProductName(runtime.nativePath);
        if (productName.empty())
        {
            productName = "HID Controller";
            if (runtime.vendorId != 0 || runtime.productId != 0)
            {
                productName += " VID_" + std::to_string(runtime.vendorId) + " PID_" + std::to_string(runtime.productId);
            }
        }

        InputDeviceInfo deviceInfo{};
        deviceInfo.device = InputDeviceRef{runtime.deviceType, 0};
        deviceInfo.backend = InputDeviceBackend::RawInputHID;
        deviceInfo.primaryBackend = InputDeviceBackend::RawInputHID;
        deviceInfo.deviceType = runtime.deviceType;
        deviceInfo.displayName = productName;
        deviceInfo.backendName = "RawInputHID";
        deviceInfo.nativeIdentity = wideToUtf8(runtime.nativePath);
        deviceInfo.nativeIdentityHash = hashWideIdentity(runtime.nativePath);
        deviceInfo.hidNativeIdentity = deviceInfo.nativeIdentity;
        deviceInfo.hidNativeIdentityHash = deviceInfo.nativeIdentityHash;
        deviceInfo.vendorId = runtime.vendorId;
        deviceInfo.productId = runtime.productId;
        deviceInfo.connected = true;
        deviceInfo.canonical = true;
        deviceInfo.hasHidFeed = true;

        InputDeviceRef mergeTarget{};
        if (runtime.deviceType == InputDeviceType::Gamepad && findBackendMergeTarget(devices, deviceInfo, mergeTarget))
        {
            runtime.device = InputInternal::InputDeviceRegistryAccess::mergeDeviceBackend(devices, mergeTarget, deviceInfo);
            debugInputBackendMessage("Merged Raw Input/HID gamepad metadata into an existing XInput canonical device.");
        }
        else
        {
            runtime.device = InputInternal::InputDeviceRegistryAccess::upsertDevice(devices, deviceInfo);
        }

        std::vector<HIDP_BUTTON_CAPS> buttonCaps(runtime.caps.NumberInputButtonCaps);
        if (!buttonCaps.empty())
        {
            USHORT buttonCount = static_cast<USHORT>(buttonCaps.size());
            if (HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCount, getPreparsedData(runtime)) == HIDP_STATUS_SUCCESS)
            {
                buttonCaps.resize(buttonCount);
                for (const HIDP_BUTTON_CAPS &cap : buttonCaps)
                {
                    buildHidButtonRuntime(runtime, cap, controls);
                }
            }
        }

        std::vector<HIDP_VALUE_CAPS> valueCaps(runtime.caps.NumberInputValueCaps);
        if (!valueCaps.empty())
        {
            USHORT valueCount = static_cast<USHORT>(valueCaps.size());
            if (HidP_GetValueCaps(HidP_Input, valueCaps.data(), &valueCount, getPreparsedData(runtime)) == HIDP_STATUS_SUCCESS)
            {
                valueCaps.resize(valueCount);
                for (const HIDP_VALUE_CAPS &cap : valueCaps)
                {
                    const USAGE usageMin = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;
                    const USAGE usageMax = cap.IsRange ? cap.Range.UsageMax : cap.NotRange.Usage;
                    for (USAGE usage = usageMin; usage <= usageMax; ++usage)
                    {
                        if (cap.UsagePage == HID_USAGE_PAGE_GENERIC)
                        {
                            buildHidValueRuntime(runtime, cap, usage, controls);
                        }
                    }
                }
            }
        }

        runtime.usable = !runtime.buttons.empty() || !runtime.values.empty();
        if (!runtime.usable)
        {
            InputInternal::InputDeviceRegistryAccess::setDeviceBackendConnected(devices, runtime.device, InputDeviceBackend::RawInputHID, false);
            const InputDeviceInfo *deviceInfo = devices.findDevice(runtime.device);
            if (deviceInfo == nullptr || !deviceInfo->connected)
            {
                InputInternal::InputDeviceRegistryAccess::setDeviceCanonical(devices, runtime.device, false);
            }
            return false;
        }

        InputInternal::InputDeviceRegistryAccess::mergeDeviceControls(devices, runtime.device, controls);
        return true;
    }

    std::wstring getRawInputDeviceName(HANDLE device)
    {
        UINT nameLength = 0;
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &nameLength) != 0 || nameLength == 0)
        {
            return {};
        }

        std::wstring name(nameLength, L'\0');
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &nameLength) == static_cast<UINT>(-1))
        {
            return {};
        }

        if (!name.empty() && name.back() == L'\0')
        {
            name.pop_back();
        }

        return name;
    }

    void refreshHidDevices(InputDeviceRegistry &devices)
    {
        hidDevices.clear();
        xInputHidIdentities.clear();
        initialXInputScanComplete = false;
        InputInternal::InputDeviceRegistryAccess::clearBackend(devices, InputDeviceBackend::RawInputHID);

        UINT deviceCount = 0;
        if (GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) != 0 || deviceCount == 0)
        {
            return;
        }

        std::vector<RAWINPUTDEVICELIST> rawDevices(deviceCount);
        if (GetRawInputDeviceList(rawDevices.data(), &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
        {
            return;
        }

        for (const RAWINPUTDEVICELIST &rawDevice : rawDevices)
        {
            if (rawDevice.dwType != RIM_TYPEHID)
            {
                continue;
            }

            RID_DEVICE_INFO deviceInfo{};
            deviceInfo.cbSize = sizeof(deviceInfo);
            UINT infoSize = sizeof(deviceInfo);
            if (GetRawInputDeviceInfoW(rawDevice.hDevice, RIDI_DEVICEINFO, &deviceInfo, &infoSize) == static_cast<UINT>(-1))
            {
                continue;
            }

            if (!isSupportedHidUsage(deviceInfo.hid.usUsagePage, deviceInfo.hid.usUsage))
            {
                continue;
            }

            HidDeviceRuntime runtime{};
            runtime.rawDevice = rawDevice.hDevice;
            runtime.deviceType = deviceInfo.hid.usUsage == HID_USAGE_GENERIC_GAMEPAD ? InputDeviceType::Gamepad : InputDeviceType::Joystick;
            runtime.nativePath = getRawInputDeviceName(rawDevice.hDevice);
            if (runtime.nativePath.empty())
            {
                continue;
            }

            enrichHidSetupMetadata(runtime);
            runtime.xInputCompatibleHid = isXInputHidRuntime(runtime);

            if (buildHidRuntime(runtime, devices))
            {
                if (runtime.xInputCompatibleHid)
                {
                    xInputHidIdentities.push_back(
                        XInputHidIdentity{
                            .nativePath = runtime.nativePath,
                            .nativeIdentity = wideToUtf8(runtime.nativePath),
                            .nativeIdentityHash = hashWideIdentity(runtime.nativePath),
                            .setupDeviceId = runtime.setupDeviceId,
                            .vendorId = runtime.vendorId,
                            .productId = runtime.productId});
                }

                hidDevices.push_back(std::move(runtime));
            }
        }
    }

    HidDeviceRuntime *findHidDevice(HANDLE rawDevice)
    {
        auto entry = std::find_if(
            hidDevices.begin(),
            hidDevices.end(),
            [rawDevice](const HidDeviceRuntime &candidate)
            {
                return candidate.rawDevice == rawDevice;
            });

        return entry != hidDevices.end() ? &(*entry) : nullptr;
    }

    void setHatButtons(InputState &inputState, const HidValueRuntime &valueRuntime, LONG rawValue)
    {
        LONG hat = rawValue;
        if (valueRuntime.logicalMinimum == 1 && valueRuntime.logicalMaximum == 8)
        {
            --hat;
        }

        const bool centered = hat < 0 || hat > 7;
        const bool up = !centered && (hat == 0 || hat == 1 || hat == 7);
        const bool right = !centered && (hat == 1 || hat == 2 || hat == 3);
        const bool down = !centered && (hat == 3 || hat == 4 || hat == 5);
        const bool left = !centered && (hat == 5 || hat == 6 || hat == 7);

        InputInternal::InputStateAccess::setButton(inputState, valueRuntime.hatButtons[0], up);
        InputInternal::InputStateAccess::setButton(inputState, valueRuntime.hatButtons[1], right);
        InputInternal::InputStateAccess::setButton(inputState, valueRuntime.hatButtons[2], down);
        InputInternal::InputStateAccess::setButton(inputState, valueRuntime.hatButtons[3], left);
    }

    void feedHidButtons(
        InputState &inputState,
        HidDeviceRuntime &runtime,
        const HidButtonRuntime &buttonRuntime,
        const char *report,
        ULONG reportSize)
    {
        if (buttonRuntime.controls.empty())
        {
            return;
        }

        ULONG usageCount = static_cast<ULONG>(buttonRuntime.controls.size());
        std::vector<USAGE> activeUsages(usageCount);
        const NTSTATUS result = HidP_GetUsages(
            HidP_Input,
            buttonRuntime.usagePage,
            buttonRuntime.linkCollection,
            activeUsages.data(),
            &usageCount,
            getPreparsedData(runtime),
            const_cast<char *>(report),
            reportSize);

        if (result != HIDP_STATUS_SUCCESS)
        {
            usageCount = 0;
        }

        activeUsages.resize(usageCount);
        for (USAGE usage = buttonRuntime.usageMin; usage <= buttonRuntime.usageMax; ++usage)
        {
            const std::size_t controlIndex = static_cast<std::size_t>(usage - buttonRuntime.usageMin);
            if (controlIndex >= buttonRuntime.controls.size())
            {
                break;
            }

            const bool isDown = std::find(activeUsages.begin(), activeUsages.end(), usage) != activeUsages.end();
            InputInternal::InputStateAccess::setButton(inputState, buttonRuntime.controls[controlIndex], isDown);
        }
    }

    void feedHidValues(InputState &inputState, HidDeviceRuntime &runtime, HidValueRuntime &valueRuntime, const char *report, ULONG reportSize)
    {
        ULONG value = 0;
        const NTSTATUS result = HidP_GetUsageValue(
            HidP_Input,
            valueRuntime.usagePage,
            valueRuntime.linkCollection,
            valueRuntime.usage,
            &value,
            getPreparsedData(runtime),
            const_cast<char *>(report),
            reportSize);

        if (result != HIDP_STATUS_SUCCESS)
        {
            return;
        }

        const LONG rawValue = static_cast<LONG>(value);
        if (valueRuntime.previousRawValue == rawValue)
        {
            return;
        }
        valueRuntime.previousRawValue = rawValue;

        if (valueRuntime.isHat)
        {
            setHatButtons(inputState, valueRuntime, rawValue);
            return;
        }

        InputInternal::InputStateAccess::setAxis(
            inputState,
            valueRuntime.control,
            normalizeHidValue(rawValue, valueRuntime.logicalMinimum, valueRuntime.logicalMaximum, runtime.deviceType, valueRuntime.usage));
    }

    float normalizeUnsignedStickByte(unsigned char value, bool invert)
    {
        float normalized =
            value < 128 ? static_cast<float>(static_cast<int>(value) - 128) / 128.0f : static_cast<float>(static_cast<int>(value) - 128) / 127.0f;
        if (invert)
        {
            normalized = -normalized;
        }

        return std::clamp(normalized, -1.0f, 1.0f);
    }

    float normalizeUnsignedTriggerByte(unsigned char value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    void feedDualSenseButton(InputState &inputState, InputDeviceRef device, GamepadButton button, bool isDown)
    {
        InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(device.deviceIndex, button), isDown);
    }

    void feedDualSenseDpad(InputState &inputState, InputDeviceRef device, unsigned char dpad)
    {
        const unsigned char direction = static_cast<unsigned char>(dpad & 0x0F);
        const bool centered = direction >= 8;
        feedDualSenseButton(inputState, device, GamepadButton::DpadUp, !centered && (direction == 0 || direction == 1 || direction == 7));
        feedDualSenseButton(inputState, device, GamepadButton::DpadRight, !centered && (direction == 1 || direction == 2 || direction == 3));
        feedDualSenseButton(inputState, device, GamepadButton::DpadDown, !centered && (direction == 3 || direction == 4 || direction == 5));
        feedDualSenseButton(inputState, device, GamepadButton::DpadLeft, !centered && (direction == 5 || direction == 6 || direction == 7));
    }

    bool tryFeedDualSenseReport(InputState &inputState, HidDeviceRuntime &runtime, const unsigned char *report, ULONG reportSize)
    {
        if (runtime.deviceType != InputDeviceType::Gamepad || runtime.vendorId != 0x054C ||
            (runtime.productId != 0x0CE6 && runtime.productId != 0x0DF2) || report == nullptr || reportSize < 12)
        {
            return false;
        }

        std::size_t commonOffset = 0;
        if (report[0] == 0x01)
        {
            commonOffset = 1;
        }
        else if (report[0] == 0x31 && reportSize >= 13)
        {
            commonOffset = 2;
        }
        else
        {
            return false;
        }

        if (commonOffset + 10 > reportSize)
        {
            return false;
        }

        const InputDeviceRef device = runtime.device;
        InputInternal::InputStateAccess::setAxis(
            inputState,
            makeGamepadAxis(device.deviceIndex, GamepadAxis::LeftX),
            normalizeUnsignedStickByte(report[commonOffset + 0], false));
        InputInternal::InputStateAccess::setAxis(
            inputState,
            makeGamepadAxis(device.deviceIndex, GamepadAxis::LeftY),
            normalizeUnsignedStickByte(report[commonOffset + 1], true));
        InputInternal::InputStateAccess::setAxis(
            inputState,
            makeGamepadAxis(device.deviceIndex, GamepadAxis::RightX),
            normalizeUnsignedStickByte(report[commonOffset + 2], false));
        InputInternal::InputStateAccess::setAxis(
            inputState,
            makeGamepadAxis(device.deviceIndex, GamepadAxis::RightY),
            normalizeUnsignedStickByte(report[commonOffset + 3], true));
        InputInternal::InputStateAccess::setAxis(
            inputState,
            makeGamepadAxis(device.deviceIndex, GamepadAxis::LeftTrigger),
            normalizeUnsignedTriggerByte(report[commonOffset + 4]));
        InputInternal::InputStateAccess::setAxis(
            inputState,
            makeGamepadAxis(device.deviceIndex, GamepadAxis::RightTrigger),
            normalizeUnsignedTriggerByte(report[commonOffset + 5]));

        const unsigned char buttons0 = report[commonOffset + 7];
        const unsigned char buttons1 = report[commonOffset + 8];
        const unsigned char buttons2 = report[commonOffset + 9];

        feedDualSenseDpad(inputState, device, buttons0);
        feedDualSenseButton(inputState, device, GamepadButton::West, (buttons0 & 0x10) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::South, (buttons0 & 0x20) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::East, (buttons0 & 0x40) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::North, (buttons0 & 0x80) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::LeftShoulder, (buttons1 & 0x01) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::RightShoulder, (buttons1 & 0x02) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::Back, (buttons1 & 0x10) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::Start, (buttons1 & 0x20) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::LeftStick, (buttons1 & 0x40) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::RightStick, (buttons1 & 0x80) != 0);
        feedDualSenseButton(inputState, device, GamepadButton::Guide, (buttons2 & 0x01) != 0);
        return true;
    }

    bool handleRawHidInput(const RAWINPUT &rawInput, InputState &inputState, InputDeviceRegistry &devices)
    {
        HidDeviceRuntime *runtime = findHidDevice(rawInput.header.hDevice);
        if (runtime == nullptr)
        {
            refreshHidDevices(devices);
            runtime = findHidDevice(rawInput.header.hDevice);
        }

        if (runtime == nullptr || !runtime->usable ||
            !InputInternal::InputDeviceRegistryAccess::shouldFeedDeviceBackend(devices, runtime->device, InputDeviceBackend::RawInputHID))
        {
            return false;
        }

        InputInternal::InputStateAccess::setDeviceConnected(inputState, runtime->device.deviceType, runtime->device.deviceIndex, true);

        const RAWHID &rawHid = rawInput.data.hid;
        const unsigned char *reportData = rawHid.bRawData;
        for (DWORD reportIndex = 0; reportIndex < rawHid.dwCount; ++reportIndex)
        {
            const std::size_t reportOffset = static_cast<std::size_t>(reportIndex) * rawHid.dwSizeHid;
            const char *report = reinterpret_cast<const char *>(reportData + reportOffset);
            const ULONG reportSize = rawHid.dwSizeHid;
            if (tryFeedDualSenseReport(inputState, *runtime, reinterpret_cast<const unsigned char *>(report), reportSize))
            {
                continue;
            }

            for (const HidButtonRuntime &buttonRuntime : runtime->buttons)
            {
                feedHidButtons(inputState, *runtime, buttonRuntime, report, reportSize);
            }

            for (HidValueRuntime &valueRuntime : runtime->values)
            {
                feedHidValues(inputState, *runtime, valueRuntime, report, reportSize);
            }
        }

        return true;
    }

    void syncRegistryConnections(InputState &inputState, const InputDeviceRegistry &devices)
    {
        for (const InputDeviceInfo &device : devices.getDevices())
        {
            InputInternal::InputStateAccess::setDeviceConnected(
                inputState,
                device.device.deviceType,
                device.device.deviceIndex,
                device.connected && device.canonical);
        }
    }

    XInputGetStateFn getXInputGetState()
    {
        if (attemptedXInputLoad)
        {
            return cachedXInputGetState;
        }
        attemptedXInputLoad = true;

        constexpr std::array<const wchar_t *, 3> xInputLibraries{L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};

        for (const wchar_t *libraryName : xInputLibraries)
        {
            HMODULE library = LoadLibraryW(libraryName);
            if (library == nullptr)
            {
                continue;
            }

            const XInputGetStateFn function = GameWIP::Base::Win32::loadProcedure<XInputGetStateFn>(library, "XInputGetState");
            if (function != nullptr)
            {
                cachedXInputGetState = function;
                return cachedXInputGetState;
            }

            FreeLibrary(library);
        }

        return nullptr;
    }

    float normalizeTrigger(BYTE value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    float normalizeThumbAxis(SHORT value)
    {
        if (value < 0)
        {
            return static_cast<float>(value) / 32768.0f;
        }

        return static_cast<float>(value) / 32767.0f;
    }

    void clearGamepadControls(InputState &inputState, DeviceIndex deviceIndex)
    {
        for (GamepadButton button : allGamepadButtons)
        {
            InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(deviceIndex, button), false);
        }

        for (GamepadAxis axis : allGamepadAxes)
        {
            InputInternal::InputStateAccess::setAxis(inputState, makeGamepadAxis(deviceIndex, axis), 0.0f);
        }
    }

    DWORD chooseDisconnectedGamepadSlot()
    {
        for (DWORD attempt = 0; attempt < maxGamepadCount; ++attempt)
        {
            DWORD userIndex = (nextDisconnectedGamepadSlotToPoll + attempt) % maxGamepadCount;
            if (!cachedGamepadConnected[static_cast<std::size_t>(userIndex)])
            {
                nextDisconnectedGamepadSlotToPoll = (userIndex + 1) % maxGamepadCount;
                return userIndex;
            }
        }

        return maxGamepadCount;
    }

    bool isRegistryBackendDevice(const InputDeviceRegistry &devices, InputDeviceRef device, InputDeviceBackend backend)
    {
        const InputDeviceInfo *deviceInfo = devices.findDevice(device);
        if (deviceInfo == nullptr)
        {
            return false;
        }

        switch (backend)
        {
        case InputDeviceBackend::BuiltIn:
            return deviceInfo->hasBuiltInFeed;
        case InputDeviceBackend::XInput:
            return deviceInfo->hasXInputFeed;
        case InputDeviceBackend::RawInputHID:
            return deviceInfo->hasHidFeed;
        }

        return false;
    }

    void markGamepadDisconnected(
        InputState &inputState,
        InputDeviceRegistry &devices,
        DeviceIndex deviceIndex,
        std::size_t cacheIndex,
        std::uint64_t clearGeneration)
    {
        const InputDeviceRef device{InputDeviceType::Gamepad, deviceIndex};
        if (!isRegistryBackendDevice(devices, device, InputDeviceBackend::XInput))
        {
            cachedGamepadInputStates[cacheIndex] = nullptr;
            cachedGamepadClearGenerations[cacheIndex] = clearGeneration;
            cachedGamepadConnected[cacheIndex] = false;
            hasCachedGamepadPacket[cacheIndex] = false;
            gamepadControlsCleared[cacheIndex] = true;
            return;
        }

        if (!gamepadControlsCleared[cacheIndex])
        {
            clearGamepadControls(inputState, deviceIndex);
            gamepadControlsCleared[cacheIndex] = true;
        }

        InputInternal::InputDeviceRegistryAccess::setDeviceBackendConnected(devices, device, InputDeviceBackend::XInput, false);
        const InputDeviceInfo *deviceInfo = devices.findDevice(device);
        InputInternal::InputStateAccess::setDeviceConnected(
            inputState,
            InputDeviceType::Gamepad,
            deviceIndex,
            deviceInfo != nullptr && deviceInfo->connected && deviceInfo->canonical);
        cachedGamepadInputStates[cacheIndex] = &inputState;
        cachedGamepadClearGenerations[cacheIndex] = clearGeneration;
        cachedGamepadConnected[cacheIndex] = false;
        hasCachedGamepadPacket[cacheIndex] = false;
    }

    void markXInputUnavailable(InputState &inputState, InputDeviceRegistry &devices, std::uint64_t clearGeneration)
    {
        for (DWORD userIndex = 0; userIndex < maxGamepadCount; ++userIndex)
        {
            std::size_t cacheIndex = static_cast<std::size_t>(userIndex);
            if (cachedGamepadConnected[cacheIndex] || !gamepadControlsCleared[cacheIndex])
            {
                DeviceIndex deviceIndex =
                    cachedGamepadConnected[cacheIndex] ? cachedGamepadDeviceIndices[cacheIndex] : static_cast<DeviceIndex>(userIndex);
                markGamepadDisconnected(inputState, devices, deviceIndex, cacheIndex, clearGeneration);
            }
            else
            {
                cachedGamepadInputStates[cacheIndex] = nullptr;
                hasCachedGamepadPacket[cacheIndex] = false;
                const InputDeviceRef device{InputDeviceType::Gamepad, static_cast<DeviceIndex>(userIndex)};
                if (isRegistryBackendDevice(devices, device, InputDeviceBackend::XInput))
                {
                    InputInternal::InputDeviceRegistryAccess::setDeviceBackendConnected(devices, device, InputDeviceBackend::XInput, false);
                }
            }
        }
    }

    void feedGamepadButton(InputState &inputState, DeviceIndex deviceIndex, WORD buttons, WORD mask, GamepadButton button)
    {
        InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(deviceIndex, button), (buttons & mask) != 0);
    }

    void feedGamepadAxis(InputState &inputState, DeviceIndex deviceIndex, GamepadAxis axis, float value)
    {
        InputInternal::InputStateAccess::setAxis(inputState, makeGamepadAxis(deviceIndex, axis), value);
    }

    void feedGamepadState(InputState &inputState, DeviceIndex deviceIndex, const XINPUT_STATE &state)
    {
        WORD buttons = state.Gamepad.wButtons;

        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_Y, GamepadButton::North);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_A, GamepadButton::South);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_B, GamepadButton::East);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_X, GamepadButton::West);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DpadUp);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DpadDown);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DpadLeft);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DpadRight);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::LeftShoulder);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::RightShoulder);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_BACK, GamepadButton::Back);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_START, GamepadButton::Start);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::LeftStick);
        feedGamepadButton(inputState, deviceIndex, buttons, XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::RightStick);

        // Standard XInput does not expose the Guide button through XInputGetState.
        InputInternal::InputStateAccess::setButton(inputState, makeGamepadButton(deviceIndex, GamepadButton::Guide), false);

        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::LeftX, normalizeThumbAxis(state.Gamepad.sThumbLX));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::LeftY, normalizeThumbAxis(state.Gamepad.sThumbLY));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::RightX, normalizeThumbAxis(state.Gamepad.sThumbRX));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::RightY, normalizeThumbAxis(state.Gamepad.sThumbRY));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::LeftTrigger, normalizeTrigger(state.Gamepad.bLeftTrigger));
        feedGamepadAxis(inputState, deviceIndex, GamepadAxis::RightTrigger, normalizeTrigger(state.Gamepad.bRightTrigger));
    }

    InputDeviceRef registerXInputDevice(InputDeviceRegistry &devices, DWORD userIndex, bool connected)
    {
        InputDeviceInfo deviceInfo{};
        deviceInfo.device = InputDeviceRef{InputDeviceType::Gamepad, static_cast<DeviceIndex>(userIndex)};
        deviceInfo.backend = InputDeviceBackend::XInput;
        deviceInfo.primaryBackend = InputDeviceBackend::XInput;
        deviceInfo.deviceType = InputDeviceType::Gamepad;
        deviceInfo.displayName = "XInput Controller " + std::to_string(userIndex + 1);
        deviceInfo.backendName = "XInput";
        deviceInfo.nativeIdentity = "xinput:" + std::to_string(userIndex);
        deviceInfo.nativeIdentityHash = static_cast<std::uint64_t>(0x58494E5055540000ull + userIndex);
        if (const XInputHidIdentity *identity = findXInputHidIdentityForSlot(userIndex); identity != nullptr)
        {
            deviceInfo.xInputNativeIdentity = identity->nativeIdentity;
            deviceInfo.xInputNativeIdentityHash = identity->nativeIdentityHash;
            deviceInfo.vendorId = identity->vendorId;
            deviceInfo.productId = identity->productId;
        }
        else
        {
            deviceInfo.xInputNativeIdentity = deviceInfo.nativeIdentity;
            deviceInfo.xInputNativeIdentityHash = deviceInfo.nativeIdentityHash;
        }
        deviceInfo.connected = connected;
        deviceInfo.canonical = connected;
        deviceInfo.hasXInputFeed = connected;

        for (GamepadButton button : allGamepadButtons)
        {
            InputControl control = makeGamepadButton(deviceInfo.device.deviceIndex, button);
            deviceInfo.controls.push_back(
                InputControlInfo{
                    .control = control,
                    .displayName = "Gamepad Button " + std::to_string(static_cast<int>(button)),
                    .minimumValue = 0.0f,
                    .maximumValue = 1.0f,
                    .relative = false});
        }

        for (GamepadAxis axis : allGamepadAxes)
        {
            InputControl control = makeGamepadAxis(deviceInfo.device.deviceIndex, axis);
            deviceInfo.controls.push_back(
                InputControlInfo{
                    .control = control,
                    .displayName = "Gamepad Axis " + std::to_string(static_cast<int>(axis)),
                    .minimumValue = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger ? 0.0f : -1.0f,
                    .maximumValue = 1.0f,
                    .relative = false});
        }

        InputDeviceRef mergeTarget{};
        if (findBackendMergeTarget(devices, deviceInfo, mergeTarget))
        {
            for (InputControlInfo &control : deviceInfo.controls)
            {
                control.control.deviceIndex = mergeTarget.deviceIndex;
            }

            deviceInfo.device = mergeTarget;
            debugInputBackendMessage("Merged XInput feed into an existing Raw Input/HID canonical gamepad; XInput is primary.");
            return InputInternal::InputDeviceRegistryAccess::mergeDeviceBackend(devices, mergeTarget, deviceInfo);
        }

        return InputInternal::InputDeviceRegistryAccess::upsertDevice(devices, deviceInfo);
    }

    /// @brief Returns whether a UTF-16 code unit is a high surrogate.
    bool isHighSurrogate(char16_t codeUnit)
    {
        return codeUnit >= 0xD800 && codeUnit <= 0xDBFF;
    }

    /// @brief Returns whether a UTF-16 code unit is a low surrogate.
    bool isLowSurrogate(char16_t codeUnit)
    {
        return codeUnit >= 0xDC00 && codeUnit <= 0xDFFF;
    }

    /// @brief Returns whether a codepoint should be treated as typed text.
    bool isTextCodepoint(char32_t codepoint)
    {
        return codepoint >= 0x20 && codepoint != 0x7F;
    }

    /// @brief Combines a UTF-16 surrogate pair into one Unicode codepoint.
    char32_t combineSurrogates(char16_t highSurrogate, char16_t lowSurrogate)
    {
        return 0x10000 + ((static_cast<char32_t>(highSurrogate) - 0xD800) << 10) + (static_cast<char32_t>(lowSurrogate) - 0xDC00);
    }

    /// @brief Feeds one UTF-16 code unit from WM_CHAR into text input.
    /// @param codeUnit UTF-16 code unit from Win32.
    /// @param inputState Input state to update.
    /// @return True if the message was consumed.
    bool feedUtf16TextCodeUnit(char16_t codeUnit, InputState &inputState)
    {
        char16_t pendingHighSurrogate = InputInternal::InputStateAccess::getPendingTextHighSurrogate(inputState);

        if (isHighSurrogate(codeUnit))
        {
            if (pendingHighSurrogate != 0)
            {
                InputInternal::InputStateAccess::addTextCodepoint(inputState, 0xFFFD);
            }
            InputInternal::InputStateAccess::setPendingTextHighSurrogate(inputState, codeUnit);
            return true;
        }

        if (isLowSurrogate(codeUnit))
        {
            if (pendingHighSurrogate != 0)
            {
                char32_t codepoint = combineSurrogates(pendingHighSurrogate, codeUnit);
                InputInternal::InputStateAccess::clearTextComposition(inputState);
                InputInternal::InputStateAccess::addTextCodepoint(inputState, codepoint);
            }
            else
            {
                InputInternal::InputStateAccess::addTextCodepoint(inputState, 0xFFFD);
            }
            return true;
        }

        if (pendingHighSurrogate != 0)
        {
            InputInternal::InputStateAccess::addTextCodepoint(inputState, 0xFFFD);
            InputInternal::InputStateAccess::clearTextComposition(inputState);
        }

        char32_t codepoint = static_cast<char32_t>(codeUnit);
        if (!isTextCodepoint(codepoint))
        {
            return true;
        }

        InputInternal::InputStateAccess::addTextCodepoint(inputState, codepoint);
        return true;
    }

    /// @brief Feeds one UTF-32 codepoint from WM_UNICHAR into text input.
    /// @param codepoint Unicode codepoint from Win32.
    /// @param inputState Input state to update.
    /// @return True if the message was consumed.
    bool feedUnicodeTextCodepoint(char32_t codepoint, InputState &inputState)
    {
        InputInternal::InputStateAccess::clearTextComposition(inputState);
        if (!isTextCodepoint(codepoint))
        {
            return true;
        }

        InputInternal::InputStateAccess::addTextCodepoint(inputState, codepoint);
        return true;
    }

    /// @brief Converts a Win32 set-1 scan code into a USB HID keyboard usage ID.
    /// @param scanCode Low-byte Win32 scan code.
    /// @param extendedE0 True for E0-prefixed scan codes.
    /// @param extendedE1 True for E1-prefixed scan codes.
    /// @param virtualKey Win32 virtual-key value from the raw keyboard packet.
    /// @return USB HID usage ID, or 0 if unsupported.
    ControlCode translateWin32ScanCodeToKeyboardControlCode(ControlCode scanCode, bool extendedE0, bool extendedE1, USHORT virtualKey)
    {
        if (virtualKey == VK_PAUSE || extendedE1)
        {
            return KeyboardControlCode::Pause;
        }

        if (extendedE0)
        {
            switch (scanCode)
            {
            case 0x1C:
                return KeyboardControlCode::KeypadEnter;
            case 0x1D:
                return KeyboardControlCode::RightControl;
            case 0x2A:
            case 0x36:
                return 0; // Fake shift packets used by some extended-key sequences.
            case 0x35:
                return KeyboardControlCode::KeypadDivide;
            case 0x37:
                return KeyboardControlCode::PrintScreen;
            case 0x38:
                return KeyboardControlCode::RightAlt;
            case 0x47:
                return KeyboardControlCode::Home;
            case 0x48:
                return KeyboardControlCode::UpArrow;
            case 0x49:
                return KeyboardControlCode::PageUp;
            case 0x4B:
                return KeyboardControlCode::LeftArrow;
            case 0x4D:
                return KeyboardControlCode::RightArrow;
            case 0x4F:
                return KeyboardControlCode::End;
            case 0x50:
                return KeyboardControlCode::DownArrow;
            case 0x51:
                return KeyboardControlCode::PageDown;
            case 0x52:
                return KeyboardControlCode::Insert;
            case 0x53:
                return KeyboardControlCode::Delete;
            case 0x5B:
                return KeyboardControlCode::LeftSuper;
            case 0x5C:
                return KeyboardControlCode::RightSuper;
            case 0x5D:
                return KeyboardControlCode::Application;
            default:
                return 0;
            }
        }

        switch (scanCode)
        {
        case 0x01:
            return KeyboardControlCode::Escape;
        case 0x02:
            return KeyboardControlCode::Digit1;
        case 0x03:
            return KeyboardControlCode::Digit2;
        case 0x04:
            return KeyboardControlCode::Digit3;
        case 0x05:
            return KeyboardControlCode::Digit4;
        case 0x06:
            return KeyboardControlCode::Digit5;
        case 0x07:
            return KeyboardControlCode::Digit6;
        case 0x08:
            return KeyboardControlCode::Digit7;
        case 0x09:
            return KeyboardControlCode::Digit8;
        case 0x0A:
            return KeyboardControlCode::Digit9;
        case 0x0B:
            return KeyboardControlCode::Digit0;
        case 0x0C:
            return KeyboardControlCode::Minus;
        case 0x0D:
            return KeyboardControlCode::Equal;
        case 0x0E:
            return KeyboardControlCode::Backspace;
        case 0x0F:
            return KeyboardControlCode::Tab;
        case 0x10:
            return KeyboardControlCode::Q;
        case 0x11:
            return KeyboardControlCode::W;
        case 0x12:
            return KeyboardControlCode::E;
        case 0x13:
            return KeyboardControlCode::R;
        case 0x14:
            return KeyboardControlCode::T;
        case 0x15:
            return KeyboardControlCode::Y;
        case 0x16:
            return KeyboardControlCode::U;
        case 0x17:
            return KeyboardControlCode::I;
        case 0x18:
            return KeyboardControlCode::O;
        case 0x19:
            return KeyboardControlCode::P;
        case 0x1A:
            return KeyboardControlCode::LeftBracket;
        case 0x1B:
            return KeyboardControlCode::RightBracket;
        case 0x1C:
            return KeyboardControlCode::Enter;
        case 0x1D:
            return KeyboardControlCode::LeftControl;
        case 0x1E:
            return KeyboardControlCode::A;
        case 0x1F:
            return KeyboardControlCode::S;
        case 0x20:
            return KeyboardControlCode::D;
        case 0x21:
            return KeyboardControlCode::F;
        case 0x22:
            return KeyboardControlCode::G;
        case 0x23:
            return KeyboardControlCode::H;
        case 0x24:
            return KeyboardControlCode::J;
        case 0x25:
            return KeyboardControlCode::K;
        case 0x26:
            return KeyboardControlCode::L;
        case 0x27:
            return KeyboardControlCode::Semicolon;
        case 0x28:
            return KeyboardControlCode::Apostrophe;
        case 0x29:
            return KeyboardControlCode::Grave;
        case 0x2A:
            return KeyboardControlCode::LeftShift;
        case 0x2B:
            return KeyboardControlCode::Backslash;
        case 0x2C:
            return KeyboardControlCode::Z;
        case 0x2D:
            return KeyboardControlCode::X;
        case 0x2E:
            return KeyboardControlCode::C;
        case 0x2F:
            return KeyboardControlCode::V;
        case 0x30:
            return KeyboardControlCode::B;
        case 0x31:
            return KeyboardControlCode::N;
        case 0x32:
            return KeyboardControlCode::M;
        case 0x33:
            return KeyboardControlCode::Comma;
        case 0x34:
            return KeyboardControlCode::Period;
        case 0x35:
            return KeyboardControlCode::Slash;
        case 0x36:
            return KeyboardControlCode::RightShift;
        case 0x37:
            return KeyboardControlCode::KeypadMultiply;
        case 0x38:
            return KeyboardControlCode::LeftAlt;
        case 0x39:
            return KeyboardControlCode::Space;
        case 0x3A:
            return KeyboardControlCode::CapsLock;
        case 0x3B:
            return KeyboardControlCode::F1;
        case 0x3C:
            return KeyboardControlCode::F2;
        case 0x3D:
            return KeyboardControlCode::F3;
        case 0x3E:
            return KeyboardControlCode::F4;
        case 0x3F:
            return KeyboardControlCode::F5;
        case 0x40:
            return KeyboardControlCode::F6;
        case 0x41:
            return KeyboardControlCode::F7;
        case 0x42:
            return KeyboardControlCode::F8;
        case 0x43:
            return KeyboardControlCode::F9;
        case 0x44:
            return KeyboardControlCode::F10;
        case 0x45:
            return KeyboardControlCode::NumLock;
        case 0x46:
            return KeyboardControlCode::ScrollLock;
        case 0x47:
            return KeyboardControlCode::Keypad7;
        case 0x48:
            return KeyboardControlCode::Keypad8;
        case 0x49:
            return KeyboardControlCode::Keypad9;
        case 0x4A:
            return KeyboardControlCode::KeypadMinus;
        case 0x4B:
            return KeyboardControlCode::Keypad4;
        case 0x4C:
            return KeyboardControlCode::Keypad5;
        case 0x4D:
            return KeyboardControlCode::Keypad6;
        case 0x4E:
            return KeyboardControlCode::KeypadPlus;
        case 0x4F:
            return KeyboardControlCode::Keypad1;
        case 0x50:
            return KeyboardControlCode::Keypad2;
        case 0x51:
            return KeyboardControlCode::Keypad3;
        case 0x52:
            return KeyboardControlCode::Keypad0;
        case 0x53:
            return KeyboardControlCode::KeypadDecimal;
        case 0x56:
            return KeyboardControlCode::NonUsBackslash;
        case 0x57:
            return KeyboardControlCode::F11;
        case 0x58:
            return KeyboardControlCode::F12;
        default:
            return virtualKey == VK_SNAPSHOT ? KeyboardControlCode::PrintScreen : 0;
        }
    }

    /// @brief Builds the physical keyboard control code used by the generic input API.
    /// @param rawKeyboard Raw keyboard packet from Win32.
    /// @return USB HID keyboard usage ID, or 0 if no usable code exists.
    ControlCode getKeyboardControlCode(const RAWKEYBOARD &rawKeyboard)
    {
        ControlCode scanCode = rawKeyboard.MakeCode & 0xFF;
        bool extendedE0 = (rawKeyboard.Flags & RI_KEY_E0) != 0;
        bool extendedE1 = (rawKeyboard.Flags & RI_KEY_E1) != 0;

        if (scanCode == 0 && rawKeyboard.VKey != 0)
        {
            UINT mappedScanCode = MapVirtualKeyW(rawKeyboard.VKey, MAPVK_VK_TO_VSC_EX);
            if (mappedScanCode == 0)
            {
                return 0;
            }

            if ((mappedScanCode & 0xFF00) == 0xE000)
            {
                extendedE0 = true;
            }
            else if ((mappedScanCode & 0xFF00) == 0xE100)
            {
                extendedE1 = true;
            }

            scanCode = mappedScanCode & 0xFF;
        }

        return translateWin32ScanCodeToKeyboardControlCode(scanCode, extendedE0, extendedE1, rawKeyboard.VKey);
    }

    /// @brief Feeds one raw mouse button transition when the matching flag is present.
    /// @param rawMouse Raw mouse packet from Win32.
    /// @param downFlag Raw Input flag for button down.
    /// @param upFlag Raw Input flag for button up.
    /// @param button Engine mouse button to update.
    /// @param inputState Input state to update.
    /// @return True if the button was updated.
    bool feedMouseButton(const RAWMOUSE &rawMouse, USHORT downFlag, USHORT upFlag, MouseButton button, InputState &inputState)
    {
        bool updated = false;
        if ((rawMouse.usButtonFlags & downFlag) != 0)
        {
            InputInternal::InputStateAccess::setButton(inputState, makeMouseButton(button), true);
            updated = true;
        }

        if ((rawMouse.usButtonFlags & upFlag) != 0)
        {
            InputInternal::InputStateAccess::setButton(inputState, makeMouseButton(button), false);
            updated = true;
        }

        return updated;
    }

    /// @brief Handles a raw mouse packet.
    /// @param rawMouse Raw mouse packet from Win32.
    /// @param inputState Input state to update.
    /// @return True when a mouse packet was handled.
    bool handleRawMouseInput(const RAWMOUSE &rawMouse, InputState &inputState)
    {
        feedMouseButton(rawMouse, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, MouseButton::Left, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, MouseButton::Right, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, MouseButton::Middle, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, MouseButton::X1, inputState);
        feedMouseButton(rawMouse, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, MouseButton::X2, inputState);

        if ((rawMouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 && (rawMouse.lLastX != 0 || rawMouse.lLastY != 0))
        {
            InputInternal::InputStateAccess::addMouseDelta(inputState, rawMouse.lLastX, rawMouse.lLastY);
        }

        if ((rawMouse.usButtonFlags & RI_MOUSE_WHEEL) != 0)
        {
            float wheelAmount = static_cast<float>(static_cast<SHORT>(rawMouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
            InputInternal::InputStateAccess::addWheelDelta(inputState, makeMouseWheel(MouseWheel::Vertical), wheelAmount);
        }

        if ((rawMouse.usButtonFlags & RI_MOUSE_HWHEEL) != 0)
        {
            float wheelAmount = static_cast<float>(static_cast<SHORT>(rawMouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
            InputInternal::InputStateAccess::addWheelDelta(inputState, makeMouseWheel(MouseWheel::Horizontal), wheelAmount);
        }

        return true;
    }

    /// @brief Extracts a signed client coordinate from a mouse message LPARAM.
    int getSignedLowWord(LPARAM lParam)
    {
        return static_cast<int>(static_cast<short>(LOWORD(lParam)));
    }

    /// @brief Extracts a signed client coordinate from a mouse message LPARAM.
    int getSignedHighWord(LPARAM lParam)
    {
        return static_cast<int>(static_cast<short>(HIWORD(lParam)));
    }

    bool feedUiMouseButton(InputState &inputState, MouseButton button, bool isDown)
    {
        InputInternal::InputStateAccess::setButton(inputState, makeMouseButton(button), isDown);
        return true;
    }

    bool feedUiMouseWheel(InputState &inputState, MouseWheel wheel, unsigned long long wParam)
    {
        float wheelAmount = static_cast<float>(GET_WHEEL_DELTA_WPARAM(static_cast<WPARAM>(wParam))) / static_cast<float>(WHEEL_DELTA);
        InputInternal::InputStateAccess::addWheelDelta(inputState, makeMouseWheel(wheel), wheelAmount);
        return true;
    }

    bool handleRawInput(LPARAM lParam, InputState &inputState, InputDeviceRegistry &devices)
    {
        HRAWINPUT rawInputHandle = reinterpret_cast<HRAWINPUT>(lParam);
        UINT bufferSize = 0;
        if (GetRawInputData(rawInputHandle, RID_INPUT, nullptr, &bufferSize, sizeof(RAWINPUTHEADER)) != 0)
        {
            return false;
        }
        if (bufferSize == 0)
        {
            return false;
        }

        thread_local std::vector<unsigned char> buffer;
        buffer.resize(bufferSize);
        UINT expectedBufferSize = bufferSize;
        UINT result = GetRawInputData(rawInputHandle, RID_INPUT, buffer.data(), &bufferSize, sizeof(RAWINPUTHEADER));
        if (result == static_cast<UINT>(-1) || result != expectedBufferSize)
        {
            return false;
        }

        RAWINPUT *rawInput = reinterpret_cast<RAWINPUT *>(buffer.data());

        if (rawInput->header.dwType == RIM_TYPEKEYBOARD)
        {
            RAWKEYBOARD &rawKeyboard = rawInput->data.keyboard;
            ControlCode controlCode = getKeyboardControlCode(rawKeyboard);
            if (controlCode != 0)
            {
                bool isDown = (rawKeyboard.Flags & RI_KEY_BREAK) == 0;
                InputInternal::InputStateAccess::setButton(inputState, makeKeyboardKey(controlCode), isDown);
                return true;
            }
        }

        if (rawInput->header.dwType == RIM_TYPEMOUSE)
        {
            return handleRawMouseInput(rawInput->data.mouse, inputState);
        }

        if (rawInput->header.dwType == RIM_TYPEHID)
        {
            return handleRawHidInput(*rawInput, inputState, devices);
        }

        return false;
    }
} // namespace

namespace GameWIP::Input::Platform::Win32
{
    void updateGamepads(InputState &inputState, InputDeviceRegistry &devices)
    {
        syncRegistryConnections(inputState, devices);

        XInputGetStateFn xInputGetState = getXInputGetState();
        std::uint64_t clearGeneration = InputInternal::InputStateAccess::getClearGeneration(inputState);

        if (xInputGetState == nullptr)
        {
            markXInputUnavailable(inputState, devices, clearGeneration);
            return;
        }

        DWORD disconnectedSlotToPoll = maxGamepadCount;
        auto now = std::chrono::steady_clock::now();
        const bool pollAllDisconnectedSlots = !initialXInputScanComplete;
        if (!pollAllDisconnectedSlots && now >= nextDisconnectedGamepadPollTime)
        {
            disconnectedSlotToPoll = chooseDisconnectedGamepadSlot();
            if (disconnectedSlotToPoll != maxGamepadCount)
            {
                nextDisconnectedGamepadPollTime = now + disconnectedGamepadPollInterval;
            }
        }

        for (DWORD userIndex = 0; userIndex < maxGamepadCount; ++userIndex)
        {
            DeviceIndex deviceIndex = static_cast<DeviceIndex>(userIndex);
            std::size_t cacheIndex = static_cast<std::size_t>(userIndex);

            bool wasConnected = cachedGamepadConnected[cacheIndex];
            if (!wasConnected && !pollAllDisconnectedSlots && userIndex != disconnectedSlotToPoll)
            {
                continue;
            }

            XINPUT_STATE state{};
            DWORD result = xInputGetState(userIndex, &state);
            if (result == ERROR_SUCCESS)
            {
                InputDeviceRef device = registerXInputDevice(devices, userIndex, true);
                deviceIndex = device.deviceIndex;
                InputInternal::InputStateAccess::setDeviceConnected(inputState, InputDeviceType::Gamepad, deviceIndex, true);
                cachedGamepadConnected[cacheIndex] = true;
                cachedGamepadDeviceIndices[cacheIndex] = deviceIndex;

                if (hasCachedGamepadPacket[cacheIndex] && cachedGamepadInputStates[cacheIndex] == &inputState &&
                    cachedGamepadPacketNumbers[cacheIndex] == state.dwPacketNumber && cachedGamepadClearGenerations[cacheIndex] == clearGeneration)
                {
                    continue;
                }

                feedGamepadState(inputState, deviceIndex, state);
                cachedGamepadPacketNumbers[cacheIndex] = state.dwPacketNumber;
                cachedGamepadClearGenerations[cacheIndex] = clearGeneration;
                cachedGamepadInputStates[cacheIndex] = &inputState;
                hasCachedGamepadPacket[cacheIndex] = true;
                gamepadControlsCleared[cacheIndex] = false;
            }
            else
            {
                if (wasConnected)
                {
                    deviceIndex = cachedGamepadDeviceIndices[cacheIndex];
                }
                markGamepadDisconnected(inputState, devices, deviceIndex, cacheIndex, clearGeneration);
            }
        }

        initialXInputScanComplete = true;
    }

    bool handleUiMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
            InputInternal::InputStateAccess::setMousePosition(inputState, getSignedLowWord(lParam), getSignedHighWord(lParam));
            (void)wParam;
            return true;
        case WM_MOUSELEAVE:
            InputInternal::InputStateAccess::clearMousePosition(inputState);
            (void)wParam;
            return true;
        case WM_LBUTTONDOWN:
            return feedUiMouseButton(inputState, MouseButton::Left, true);
        case WM_LBUTTONUP:
            return feedUiMouseButton(inputState, MouseButton::Left, false);
        case WM_RBUTTONDOWN:
            return feedUiMouseButton(inputState, MouseButton::Right, true);
        case WM_RBUTTONUP:
            return feedUiMouseButton(inputState, MouseButton::Right, false);
        case WM_MBUTTONDOWN:
            return feedUiMouseButton(inputState, MouseButton::Middle, true);
        case WM_MBUTTONUP:
            return feedUiMouseButton(inputState, MouseButton::Middle, false);
        case WM_XBUTTONDOWN:
            return feedUiMouseButton(
                inputState,
                GET_XBUTTON_WPARAM(static_cast<WPARAM>(wParam)) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2,
                true);
        case WM_XBUTTONUP:
            return feedUiMouseButton(
                inputState,
                GET_XBUTTON_WPARAM(static_cast<WPARAM>(wParam)) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2,
                false);
        case WM_MOUSEWHEEL:
            return feedUiMouseWheel(inputState, MouseWheel::Vertical, wParam);
        case WM_MOUSEHWHEEL:
            return feedUiMouseWheel(inputState, MouseWheel::Horizontal, wParam);
        case WM_CHAR:
            feedUtf16TextCodeUnit(static_cast<char16_t>(wParam), inputState);
            return true;
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR)
            {
                return true;
            }

            feedUnicodeTextCodepoint(static_cast<char32_t>(wParam), inputState);
            return true;
        default:
            break;
        }

        (void)wParam;
        (void)lParam;
        return false;
    }

    bool handleMessage(unsigned int message, unsigned long long wParam, long long lParam, InputState &inputState, InputDeviceRegistry &devices)
    {
        if (message == WM_INPUT)
        {
            return handleRawInput(lParam, inputState, devices);
        }

        if (message == WM_INPUT_DEVICE_CHANGE)
        {
            refreshHidDevices(devices);
            inputState.clear();
            syncRegistryConnections(inputState, devices);
            (void)wParam;
            (void)lParam;
            return true;
        }

        return handleUiMessage(message, wParam, lParam, inputState);
    }

    bool registerInputDevices(void *windowHandle, InputDeviceRegistry &devices, unsigned long &win32Error)
    {
        win32Error = 0;

        if (windowHandle == nullptr)
        {
            win32Error = ERROR_INVALID_HANDLE;
            return false;
        }

        HWND hwnd = reinterpret_cast<HWND>(windowHandle);

        RAWINPUTDEVICE rawDevices[5]{};
        // Mouse
        rawDevices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rawDevices[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        rawDevices[0].dwFlags = RIDEV_DEVNOTIFY;
        rawDevices[0].hwndTarget = hwnd;

        // Keyboard
        rawDevices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rawDevices[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
        rawDevices[1].dwFlags = RIDEV_DEVNOTIFY;
        rawDevices[1].hwndTarget = hwnd;

        // Native HID controllers.
        rawDevices[2].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rawDevices[2].usUsage = HID_USAGE_GENERIC_GAMEPAD;
        rawDevices[2].dwFlags = RIDEV_DEVNOTIFY;
        rawDevices[2].hwndTarget = hwnd;

        rawDevices[3].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rawDevices[3].usUsage = HID_USAGE_GENERIC_JOYSTICK;
        rawDevices[3].dwFlags = RIDEV_DEVNOTIFY;
        rawDevices[3].hwndTarget = hwnd;

        rawDevices[4].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rawDevices[4].usUsage = HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER;
        rawDevices[4].dwFlags = RIDEV_DEVNOTIFY;
        rawDevices[4].hwndTarget = hwnd;

        if (!RegisterRawInputDevices(rawDevices, static_cast<UINT>(5), sizeof(RAWINPUTDEVICE)))
        {
            win32Error = GetLastError();
            return false;
        }

        refreshHidDevices(devices);
        return true;
    }
} // namespace GameWIP::Input::Platform::Win32
