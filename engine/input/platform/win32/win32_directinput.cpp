/// @file win32_directinput.cpp
/// @brief Windows DirectInput device discovery and polling.

#include "input/platform/win32/internal/win32_attachment_state.h"
#include "input/platform/win32/internal/device_helpers.h"
#include "input/platform/win32/internal/device_metadata.h"
#include "input/platform/win32/internal/win32_unicode.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <new>
#include <string>
#include <string_view>

namespace GameWIP::Input::Platform::Win32
{
    namespace
    {
        using DirectInput8CreateFunction = HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
        using DirectInputDevice = IDirectInputDevice8W;

        struct EnumerationContext
        {
            // Enumeration target
            AttachmentState *attachment = nullptr;
            SystemState *state = nullptr;
            DeviceRole role = DeviceRole::DirectInputController;
            const DIDATAFORMAT *format = nullptr;

            // Enumeration progress
            std::uint32_t nextSlot = 1;
            std::uint32_t failureCode = static_cast<std::uint32_t>(E_OUTOFMEMORY);
            bool failed = false;
        };

        struct ObjectContext
        {
            // Bound device
            SystemState *state = nullptr;
            Types::DeviceInfo *descriptor = nullptr;
            DeviceBinding *binding = nullptr;
            DirectInputDevice *device = nullptr;
            std::string_view persistentKey{};

            // Object indices and status
            std::size_t axisIndex = 0;
            std::size_t buttonIndex = 0;
            std::size_t switchIndex = 0;
            bool failed = false;
        };

        struct DirectInputDataFormat
        {
            std::array<DIOBJECTDATAFORMAT, 256> objects{};
            DIDATAFORMAT format{};
        };

        /// ------------------------------------------------------------
        /// DirectInput data formats
        /// ------------------------------------------------------------

        [[nodiscard]] const DIDATAFORMAT &keyboardDataFormat() noexcept
        {
            static DirectInputDataFormat data{};
            static const bool initialized = []
            {
                for(std::size_t index = 0; index < data.objects.size(); ++index)
                {
                    data.objects[index] = {
                        .pguid = &GUID_Key,
                        .dwOfs = static_cast<DWORD>(index),
                        .dwType = static_cast<DWORD>(DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(index)),
                        .dwFlags = 0,
                    };
                }
                data.format = {
                    .dwSize = sizeof(DIDATAFORMAT),
                    .dwObjSize = sizeof(DIOBJECTDATAFORMAT),
                    .dwFlags = 0,
                    .dwDataSize = 256,
                    .dwNumObjs = static_cast<DWORD>(data.objects.size()),
                    .rgodf = data.objects.data(),
                };
                return true;
            }();
            (void)initialized;
            return data.format;
        }

        [[nodiscard]] const DIDATAFORMAT &mouseDataFormat() noexcept
        {
            static DirectInputDataFormat data{};
            static const bool initialized = []
            {
                data.objects[0] = {.pguid = &GUID_XAxis, .dwOfs = offsetof(DIMOUSESTATE2, lX), .dwType = DIDFT_RELAXIS, .dwFlags = 0};
                data.objects[1] = {.pguid = &GUID_YAxis, .dwOfs = offsetof(DIMOUSESTATE2, lY), .dwType = DIDFT_RELAXIS, .dwFlags = 0};
                data.objects[2] = {.pguid = &GUID_ZAxis, .dwOfs = offsetof(DIMOUSESTATE2, lZ), .dwType = DIDFT_RELAXIS, .dwFlags = 0};
                for(std::size_t index = 0; index < 8; ++index)
                {
                    data.objects[index + 3] = {
                        .pguid = &GUID_Button,
                        .dwOfs = static_cast<DWORD>(offsetof(DIMOUSESTATE2, rgbButtons) + index),
                        .dwType = static_cast<DWORD>(DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(index)),
                        .dwFlags = 0,
                    };
                }
                data.format = {
                    .dwSize = sizeof(DIDATAFORMAT),
                    .dwObjSize = sizeof(DIOBJECTDATAFORMAT),
                    .dwFlags = DIDF_RELAXIS,
                    .dwDataSize = sizeof(DIMOUSESTATE2),
                    .dwNumObjs = 11,
                    .rgodf = data.objects.data(),
                };
                return true;
            }();
            (void)initialized;
            return data.format;
        }

        [[nodiscard]] const DIDATAFORMAT &controllerDataFormat() noexcept
        {
            static DirectInputDataFormat data{};
            static const bool initialized = []
            {
                const std::array<std::pair<const GUID *, DWORD>, 8> axes{{
                    {&GUID_XAxis, static_cast<DWORD>(offsetof(DIJOYSTATE2, lX))},
                    {&GUID_YAxis, static_cast<DWORD>(offsetof(DIJOYSTATE2, lY))},
                    {&GUID_ZAxis, static_cast<DWORD>(offsetof(DIJOYSTATE2, lZ))},
                    {&GUID_RxAxis, static_cast<DWORD>(offsetof(DIJOYSTATE2, lRx))},
                    {&GUID_RyAxis, static_cast<DWORD>(offsetof(DIJOYSTATE2, lRy))},
                    {&GUID_RzAxis, static_cast<DWORD>(offsetof(DIJOYSTATE2, lRz))},
                    {&GUID_Slider, static_cast<DWORD>(offsetof(DIJOYSTATE2, rglSlider))},
                    {&GUID_Slider, static_cast<DWORD>(offsetof(DIJOYSTATE2, rglSlider) + sizeof(LONG))},
                }};
                for(std::size_t index = 0; index < axes.size(); ++index)
                {
                    data.objects[index] = {
                        .pguid = axes[index].first,
                        .dwOfs = axes[index].second,
                        .dwType = static_cast<DWORD>(DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(index)),
                        .dwFlags = 0,
                    };
                }
                for(std::size_t index = 0; index < 4; ++index)
                {
                    data.objects[8 + index] = {
                        .pguid = &GUID_POV,
                        .dwOfs = static_cast<DWORD>(offsetof(DIJOYSTATE2, rgdwPOV) + index * sizeof(DWORD)),
                        .dwType = static_cast<DWORD>(DIDFT_POV | DIDFT_MAKEINSTANCE(index)),
                        .dwFlags = 0,
                    };
                }
                for(std::size_t index = 0; index < 128; ++index)
                {
                    data.objects[12 + index] = {
                        .pguid = &GUID_Button,
                        .dwOfs = static_cast<DWORD>(offsetof(DIJOYSTATE2, rgbButtons) + index),
                        .dwType = static_cast<DWORD>(DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(index)),
                        .dwFlags = 0,
                    };
                }
                data.format = {
                    .dwSize = sizeof(DIDATAFORMAT),
                    .dwObjSize = sizeof(DIOBJECTDATAFORMAT),
                    .dwFlags = DIDF_ABSAXIS,
                    .dwDataSize = sizeof(DIJOYSTATE2),
                    .dwNumObjs = 140,
                    .rgodf = data.objects.data(),
                };
                return true;
            }();
            (void)initialized;
            return data.format;
        }

        [[nodiscard]] std::string instanceKey(REFGUID guid)
        {
            std::array<wchar_t, 40> text{};
            const int length = StringFromGUID2(guid, text.data(), static_cast<int>(text.size()));
            return length <= 1 ? std::string{} : wideToUtf8(std::wstring_view(text.data(), static_cast<std::size_t>(length - 1)));
        }

        [[nodiscard]] bool sameInstance(const DeviceBinding &binding, REFGUID guid) noexcept
        {
            return (binding.role == DeviceRole::DirectInputKeyboard || binding.role == DeviceRole::DirectInputMouse ||
                    binding.role == DeviceRole::DirectInputController) &&
                   IsEqualGUID(binding.directInputInstance, guid) != FALSE;
        }

        [[nodiscard]] DeviceBinding *findDirectInputDevice(AttachmentState &attachment, REFGUID guid) noexcept
        {
            const auto found = std::find_if(
                attachment.devices.begin(),
                attachment.devices.end(),
                [guid](const DeviceBinding &binding)
                {
                    return sameInstance(binding, guid);
                });
            return found == attachment.devices.end() ? nullptr : &(*found);
        }

        [[nodiscard]] std::string objectName(const DIDEVICEOBJECTINSTANCEW &object, std::string_view fallback)
        {
            const std::wstring_view name(object.tszName, std::char_traits<wchar_t>::length(object.tszName));
            std::string result = wideToUtf8(name);
            return result.empty() ? std::string(fallback) : result;
        }

        [[nodiscard]] bool objectOffsetFits(DWORD offset, std::size_t size, std::size_t extent) noexcept
        {
            const std::size_t nativeOffset = static_cast<std::size_t>(offset);
            return nativeOffset <= extent && size <= extent - nativeOffset;
        }

        /// ------------------------------------------------------------
        /// Device descriptor enumeration
        /// ------------------------------------------------------------

        BOOL CALLBACK addDirectInputObject(const DIDEVICEOBJECTINSTANCEW *object, void *contextPointer) noexcept
        {
            auto &context = *static_cast<ObjectContext *>(contextPointer);
            if(object == nullptr || context.state == nullptr || context.descriptor == nullptr || context.binding == nullptr)
            {
                return DIENUM_CONTINUE;
            }

            try
            {
                if((object->dwType & DIDFT_AXIS) != 0 && objectOffsetFits(object->dwOfs, sizeof(LONG), sizeof(DIJOYSTATE2)))
                {
                    std::optional<Types::Scalar> minimum{};
                    std::optional<Types::Scalar> maximum{};
                    if(context.device != nullptr)
                    {
                        DIPROPRANGE range{
                            .diph =
                                {.dwSize = sizeof(DIPROPRANGE), .dwHeaderSize = sizeof(DIPROPHEADER), .dwObj = object->dwOfs, .dwHow = DIPH_BYOFFSET},
                        };
                        if(SUCCEEDED(context.device->GetProperty(DIPROP_RANGE, &range.diph)) && range.lMin <= range.lMax)
                        {
                            minimum = static_cast<std::int64_t>(range.lMin);
                            maximum = static_cast<std::int64_t>(range.lMax);
                        }
                    }

                    const std::string fallback = std::format("Axis {}", context.axisIndex + 1);
                    ++context.axisIndex;
                    addElement(
                        *context.state,
                        *context.descriptor,
                        *context.binding,
                        objectName(*object, fallback),
                        std::string(context.persistentKey) + ":axis:" + std::to_string(object->dwOfs),
                        std::nullopt,
                        Types::ValueKind::SignedInteger,
                        Types::ReportingMode::Absolute,
                        {Types::ComponentInfo{
                            .index = 0,
                            .name = "Value",
                            .numeric =
                                Types::NumericInfo{
                                    .nativeStorage = Types::NumericStorage::Signed32,
                                    .minimum = minimum,
                                    .maximum = maximum,
                                },
                        }},
                        ElementRole::NativeAxis);
                    context.binding->elements.back().nativeIndex = object->dwOfs;
                    return DIENUM_CONTINUE;
                }

                if((object->dwType & DIDFT_BUTTON) != 0 && object->dwOfs >= offsetof(DIJOYSTATE2, rgbButtons) &&
                   objectOffsetFits(object->dwOfs, sizeof(BYTE), offsetof(DIJOYSTATE2, rgbButtons) + sizeof(DIJOYSTATE2::rgbButtons)))
                {
                    const std::string fallback = std::format("Button {}", context.buttonIndex + 1);
                    addElement(
                        *context.state,
                        *context.descriptor,
                        *context.binding,
                        objectName(*object, fallback),
                        std::string(context.persistentKey) + ":button:" + std::to_string(context.buttonIndex),
                        std::nullopt,
                        Types::ValueKind::Boolean,
                        Types::ReportingMode::Absolute,
                        {},
                        ElementRole::NativeButton);
                    context.binding->elements.back().nativeIndex = object->dwOfs;
                    ++context.buttonIndex;
                    return DIENUM_CONTINUE;
                }

                if((object->dwType & DIDFT_POV) != 0 && objectOffsetFits(object->dwOfs, sizeof(DWORD), sizeof(DIJOYSTATE2)))
                {
                    const std::string fallback = std::format("Hat {}", context.switchIndex + 1);
                    addElement(
                        *context.state,
                        *context.descriptor,
                        *context.binding,
                        objectName(*object, fallback),
                        std::string(context.persistentKey) + ":switch:" + std::to_string(context.switchIndex),
                        std::nullopt,
                        Types::ValueKind::UnsignedInteger,
                        Types::ReportingMode::Absolute,
                        {Types::ComponentInfo{
                            .index = 0,
                            .name = "Position",
                            .numeric =
                                Types::NumericInfo{
                                    .nativeStorage = Types::NumericStorage::Unsigned32,
                                    .minimum = std::uint64_t{0},
                                    .maximum = std::uint64_t{35999},
                                    .nullStateSupported = true,
                                },
                        }},
                        ElementRole::NativeSwitch);
                    context.binding->elements.back().nativeIndex = object->dwOfs;
                    ++context.switchIndex;
                }
            }
            catch(...)
            {
                context.failed = true;
                return DIENUM_STOP;
            }
            return DIENUM_CONTINUE;
        }

        [[nodiscard]] std::string directInputPath(DirectInputDevice &device, std::wstring &widePath)
        {
            DIPROPGUIDANDPATH path{
                .diph = {.dwSize = sizeof(DIPROPGUIDANDPATH), .dwHeaderSize = sizeof(DIPROPHEADER), .dwObj = 0, .dwHow = DIPH_DEVICE},
            };
            if(FAILED(device.GetProperty(DIPROP_GUIDANDPATH, &path.diph)))
            {
                return {};
            }
            widePath.assign(path.wszPath);
            return wideToUtf8(widePath);
        }

        [[nodiscard]] bool initializeDevice(EnumerationContext &context, const DIDEVICEINSTANCEW &instance, DirectInputDevice **outDevice) noexcept
        {
            auto *directInput = static_cast<IDirectInput8W *>(context.attachment->directInput);
            DirectInputDevice *device = nullptr;
            HRESULT result = directInput->CreateDevice(instance.guidInstance, &device, nullptr);
            if(FAILED(result) || device == nullptr)
            {
                return false;
            }

            result = device->SetDataFormat(context.format);
            if(SUCCEEDED(result))
            {
                result = device->SetCooperativeLevel(reinterpret_cast<HWND>(context.attachment->windowHandle), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
            }

            if(FAILED(result))
            {
                device->Release();
                return false;
            }

            (void)device->Acquire();
            *outDevice = device;
            return true;
        }

        BOOL CALLBACK addDirectInputDevice(const DIDEVICEINSTANCEW *instance, void *contextPointer) noexcept
        {
            auto &context = *static_cast<EnumerationContext *>(contextPointer);
            if(instance == nullptr || context.attachment == nullptr || context.state == nullptr)
            {
                return DIENUM_CONTINUE;
            }

            try
            {
                const std::uint32_t slot = context.nextSlot++;

                // Reconnect a matching native instance without rebuilding its descriptor.
                DeviceBinding *existing = findDirectInputDevice(*context.attachment, instance->guidInstance);
                if(existing != nullptr && existing->nativeDevice != nullptr)
                {
                    existing->nativeSlot = slot;
                    reconnectDevice(*context.state, *existing);
                    return DIENUM_CONTINUE;
                }

                DirectInputDevice *device = nullptr;
                if(!initializeDevice(context, *instance, &device))
                {
                    return DIENUM_CONTINUE;
                }

                // Resolve the native path and logical identity used to merge sources.
                std::wstring widePath;
                const std::string path = directInputPath(*device, widePath);
                const std::string sourceKey =
                    path.empty() ? "win32:directinput:instance:" + instanceKey(instance->guidInstance) : "win32:device:path:" + path;
                const std::string containerKey = path.empty() ? std::string{} : containerPersistentKey(widePath);
                const std::string logicalKey = containerKey.empty() ? sourceKey : containerKey;
                const Types::IdentityConfidence identityConfidence =
                    containerKey.empty() ? Types::IdentityConfidence::ConnectionPath : Types::IdentityConfidence::DeviceUnique;
                const std::string deviceName = wideToUtf8(instance->tszProductName);
                const std::string fallbackName = context.role == DeviceRole::DirectInputKeyboard
                                                     ? "Keyboard"
                                                     : (context.role == DeviceRole::DirectInputMouse ? "Mouse" : "DirectInput Controller");
                Types::SourceInfo source{
                    .backend = Types::BackendKind::DirectInput,
                    .backendName = "Microsoft DirectInput",
                    .active = true,
                    .persistentKey = sourceKey,
                    .identityConfidence = Types::IdentityConfidence::ConnectionPath,
                };

                // Merge this source into an existing logical device when possible.
                if(existing != nullptr)
                {
                    existing->nativeDevice = device;
                    existing->nativeSlot = slot;
                    if(!existing->connected)
                    {
                        reconnectDevice(*context.state, *existing);
                    }
                    return DIENUM_CONTINUE;
                }

                // Create the descriptor and retain its DirectInput binding.
                const std::size_t bindingCount = context.attachment->devices.size();
                addDevice(
                    *context.state,
                    *context.attachment,
                    context.role,
                    slot,
                    deviceName.empty() ? fallbackName : deviceName,
                    {},
                    true,
                    std::move(source),
                    logicalKey,
                    identityConfidence);
                if(context.attachment->devices.size() == bindingCount)
                {
                    device->Release();
                    return DIENUM_CONTINUE;
                }

                DeviceBinding &binding = context.attachment->devices.back();
                binding.nativeDevice = device;
                binding.directInputInstance = instance->guidInstance;
                binding.nativeSlot = slot;

                const std::size_t descriptorIndex = context.state->findDeviceIndex(binding.id);
                if(descriptorIndex == context.state->devices.size())
                {
                    device->Release();
                    binding.nativeDevice = nullptr;
                    context.failed = true;
                    context.failureCode = static_cast<std::uint32_t>(E_FAIL);
                    return DIENUM_STOP;
                }

                // Add provider-specific controls after the descriptor is registered.
                if(context.role == DeviceRole::DirectInputController)
                {
                    ObjectContext objectContext{
                        .state = context.state,
                        .descriptor = &context.state->devices[descriptorIndex],
                        .binding = &binding,
                        .device = device,
                        .persistentKey = sourceKey,
                    };
                    const HRESULT objectResult = device->EnumObjects(addDirectInputObject, &objectContext, DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
                    if(FAILED(objectResult) || objectContext.failed)
                    {
                        context.failed = true;
                        context.failureCode =
                            FAILED(objectResult) ? static_cast<std::uint32_t>(objectResult) : static_cast<std::uint32_t>(E_OUTOFMEMORY);
                        return DIENUM_STOP;
                    }
                }
                else if(context.role == DeviceRole::DirectInputMouse)
                {
                    constexpr std::size_t kStandardMouseButtons = 5;
                    for(std::size_t index = kStandardMouseButtons; index < 8; ++index)
                    {
                        addElement(
                            *context.state,
                            context.state->devices[descriptorIndex],
                            binding,
                            std::format("Button {}", index + 1),
                            sourceKey + ":button:" + std::to_string(index),
                            std::nullopt,
                            Types::ValueKind::Boolean,
                            Types::ReportingMode::Absolute,
                            {},
                            ElementRole::NativeButton);
                        binding.elements.back().nativeIndex = static_cast<std::uint32_t>(offsetof(DIMOUSESTATE2, rgbButtons) + index);
                    }
                }
            }
            catch(...)
            {
                context.failed = true;
                return DIENUM_STOP;
            }
            return DIENUM_CONTINUE;
        }

        void releaseDirectInputDevices(AttachmentState &attachment) noexcept
        {
            for(DeviceBinding &binding : attachment.devices)
            {
                if(binding.role == DeviceRole::DirectInputKeyboard || binding.role == DeviceRole::DirectInputMouse ||
                   binding.role == DeviceRole::DirectInputController)
                {
                    auto *device = static_cast<DirectInputDevice *>(binding.nativeDevice);
                    if(device != nullptr)
                    {
                        device->Unacquire();
                        device->Release();
                        binding.nativeDevice = nullptr;
                    }
                }
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Provider lifecycle
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status initializeDirectInput(AttachmentState &attachment) noexcept
    {
        // Initialize COM before loading DirectInput.
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if(SUCCEEDED(comResult))
        {
            attachment.directInputOwnsCom = true;
        }
        else if(comResult != RPC_E_CHANGED_MODE)
        {
            return nativeStatus(static_cast<std::uint32_t>(comResult), GameWIP::Status::Types::NativeCodeDomain::HResult);
        }

        // Load the system runtime and resolve its entry point dynamically.
        HMODULE module = LoadLibraryExW(L"dinput8.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(module == nullptr)
        {
            if(attachment.directInputOwnsCom)
            {
                CoUninitialize();
                attachment.directInputOwnsCom = false;
            }
            return status(GameWIP::Status::Types::Code::Unsupported);
        }

        static_assert(sizeof(DirectInput8CreateFunction) == sizeof(FARPROC));
        const DirectInput8CreateFunction create = std::bit_cast<DirectInput8CreateFunction>(GetProcAddress(module, "DirectInput8Create"));
        if(create == nullptr)
        {
            FreeLibrary(module);
            if(attachment.directInputOwnsCom)
            {
                CoUninitialize();
                attachment.directInputOwnsCom = false;
            }
            return status(GameWIP::Status::Types::Code::Unsupported);
        }

        // Create and retain the DirectInput interface.
        IDirectInput8W *directInput = nullptr;
        const HRESULT result =
            create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&directInput), nullptr);
        if(FAILED(result) || directInput == nullptr)
        {
            FreeLibrary(module);
            if(attachment.directInputOwnsCom)
            {
                CoUninitialize();
                attachment.directInputOwnsCom = false;
            }
            return nativeStatus(static_cast<std::uint32_t>(result), GameWIP::Status::Types::NativeCodeDomain::HResult);
        }

        attachment.directInputModule = module;
        attachment.directInput = directInput;
        return GameWIP::Status::Types::Status{};
    }

    void shutdownDirectInput(AttachmentState &attachment) noexcept
    {
        releaseDirectInputDevices(attachment);

        auto *directInput = static_cast<IDirectInput8W *>(attachment.directInput);
        if(directInput != nullptr)
        {
            directInput->Release();
        }

        if(attachment.directInputModule != nullptr)
        {
            FreeLibrary(attachment.directInputModule);
        }

        if(attachment.directInputOwnsCom)
        {
            CoUninitialize();
        }

        attachment.directInput = nullptr;
        attachment.directInputModule = nullptr;
        attachment.directInputOwnsCom = false;
    }

    /// ------------------------------------------------------------
    /// Device enumeration
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status enumerateDirectInputDevices(AttachmentState &attachment, SystemState &state) noexcept
    {
        auto *directInput = static_cast<IDirectInput8W *>(attachment.directInput);
        if(directInput == nullptr)
        {
            return status(GameWIP::Status::Types::Code::Unsupported);
        }

        try
        {
            // Mark prior bindings unseen; enumeration below marks current devices.
            for(DeviceBinding &binding : attachment.devices)
            {
                if(binding.role == DeviceRole::DirectInputKeyboard || binding.role == DeviceRole::DirectInputMouse ||
                   binding.role == DeviceRole::DirectInputController)
                {
                    binding.nativeSlot = 0;
                }
            }

            // Enumerate controllers, then keyboard and mouse fallbacks if Raw Input is disabled.
            EnumerationContext context{
                .attachment = &attachment,
                .state = &state,
                .role = DeviceRole::DirectInputController,
                .format = &controllerDataFormat(),
            };
            HRESULT result = directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, addDirectInputDevice, &context, DIEDFL_ATTACHEDONLY);

            if(SUCCEEDED(result) && !attachment.rawInputEnabled)
            {
                context.role = DeviceRole::DirectInputKeyboard;
                context.format = &keyboardDataFormat();
                result = directInput->EnumDevices(DI8DEVCLASS_KEYBOARD, addDirectInputDevice, &context, DIEDFL_ATTACHEDONLY);
            }

            if(SUCCEEDED(result) && !attachment.rawInputEnabled)
            {
                context.role = DeviceRole::DirectInputMouse;
                context.format = &mouseDataFormat();
                result = directInput->EnumDevices(DI8DEVCLASS_POINTER, addDirectInputDevice, &context, DIEDFL_ATTACHEDONLY);
            }

            // Disconnect and release bindings that were not seen in this pass.
            for(DeviceBinding &binding : attachment.devices)
            {
                if((binding.role == DeviceRole::DirectInputKeyboard || binding.role == DeviceRole::DirectInputMouse ||
                    binding.role == DeviceRole::DirectInputController) &&
                   binding.nativeSlot == 0)
                {
                    invalidateDevice(state, attachment, binding, Types::EventCause::Disconnected);
                    auto *device = static_cast<DirectInputDevice *>(binding.nativeDevice);
                    if(device != nullptr)
                    {
                        device->Unacquire();
                        device->Release();
                        binding.nativeDevice = nullptr;
                    }
                }
            }

            if(FAILED(result) || context.failed)
            {
                return nativeStatus(
                    context.failed ? context.failureCode : static_cast<std::uint32_t>(result),
                    GameWIP::Status::Types::NativeCodeDomain::HResult);
            }
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            state.pendingDiscontinuity = true;
            return status(GameWIP::Status::Types::Code::OutOfMemory);
        }
    }

} // namespace GameWIP::Input::Platform::Win32
