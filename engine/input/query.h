/// @file query.h
/// @brief Allocation-free helpers for resolving input descriptors.
#pragma once

#include "input/types.h"

#include <optional>
#include <span>
#include <string_view>

/// @brief Descriptor queries for the GameWIP Input API.
namespace GameWIP::Input::Query
{
    /// @brief Tests one element descriptor against a persistent selector.
    /// @param element Element descriptor to test.
    /// @param selector Persistent-key, standard, HID, value-kind, and reporting criteria.
    /// @return `true` when every criterion present in selector matches.
    [[nodiscard]] inline bool matches(const Types::ElementInfo &element, const Types::ElementSelector &selector) noexcept
    {
        if (!selector.isValid())
        {
            return false;
        }
        if (selector.persistentKey.has_value() && element.persistentKey != *selector.persistentKey)
        {
            return false;
        }
        if (selector.standard.has_value() && (!element.identity.standard.has_value() || *element.identity.standard != *selector.standard))
        {
            return false;
        }
        if (selector.hid.has_value() && (!element.identity.hid.has_value() || *element.identity.hid != *selector.hid))
        {
            return false;
        }
        if (selector.kind.has_value() && element.kind != *selector.kind)
        {
            return false;
        }
        if (selector.reporting.has_value() && element.reporting != *selector.reporting)
        {
            return false;
        }
        return true;
    }

    /// @brief Finds one device by runtime identity in a descriptor span.
    /// @param devices Current device descriptors.
    /// @param id Runtime device identity.
    /// @return Borrowed descriptor, or null when absent.
    [[nodiscard]] inline const Types::DeviceInfo *findDevice(std::span<const Types::DeviceInfo> devices, Types::DeviceId id) noexcept
    {
        if (!id.isValid())
        {
            return nullptr;
        }
        for (const Types::DeviceInfo &device : devices)
        {
            if (device.id == id)
            {
                return &device;
            }
        }
        return nullptr;
    }

    /// @brief Finds one device by a nonempty persistent identity.
    /// @param devices Current device descriptors.
    /// @param persistentKey Backend-provided persistent device key.
    /// @return Borrowed descriptor, or null when absent.
    [[nodiscard]] inline const Types::DeviceInfo *findDeviceByPersistentKey(
        std::span<const Types::DeviceInfo> devices,
        std::string_view persistentKey) noexcept
    {
        if (persistentKey.empty())
        {
            return nullptr;
        }
        for (const Types::DeviceInfo &device : devices)
        {
            if (device.persistentKey == persistentKey)
            {
                return &device;
            }
        }
        return nullptr;
    }

    /// @brief Finds one element by runtime identity in a device span.
    /// @param devices Current device descriptors.
    /// @param id Runtime element identity.
    /// @return Borrowed descriptor, or null when absent.
    [[nodiscard]] inline const Types::ElementInfo *findElement(std::span<const Types::DeviceInfo> devices, Types::ElementId id) noexcept
    {
        if (!id.isValid())
        {
            return nullptr;
        }
        const Types::DeviceInfo *device = findDevice(devices, id.device);
        if (device == nullptr)
        {
            return nullptr;
        }
        for (const Types::ElementInfo &element : device->elements)
        {
            if (element.id == id)
            {
                return &element;
            }
        }
        return nullptr;
    }

    /// @brief Finds the first element matching a selector.
    /// @param devices Current device descriptors.
    /// @param selector Persistent element criteria.
    /// @param device Device to search; an invalid ID searches connected devices, while a valid ID also searches disconnected devices.
    /// @return Borrowed descriptor, or null when no element matches.
    [[nodiscard]] inline const Types::ElementInfo *findElement(
        std::span<const Types::DeviceInfo> devices,
        const Types::ElementSelector &selector,
        Types::DeviceId device = {}) noexcept
    {
        if (!selector.isValid())
        {
            return nullptr;
        }
        for (const Types::DeviceInfo &candidate : devices)
        {
            if (device.isValid() ? candidate.id != device : !candidate.connected)
            {
                continue;
            }
            for (const Types::ElementInfo &element : candidate.elements)
            {
                if (matches(element, selector))
                {
                    return &element;
                }
            }
        }
        return nullptr;
    }

    /// @brief Finds the first element with a producer-defined persistent key.
    /// @param devices Current device descriptors.
    /// @param persistentKey Stable element binding key.
    /// @param device Device to search; an invalid ID searches connected devices, while a valid ID also searches disconnected devices.
    /// @return Borrowed descriptor, or null when no element matches.
    [[nodiscard]] inline const Types::ElementInfo *findElementByPersistentKey(
        std::span<const Types::DeviceInfo> devices,
        std::string_view persistentKey,
        Types::DeviceId device = {}) noexcept
    {
        return findElement(devices, Types::ElementSelector{.persistentKey = persistentKey}, device);
    }

    /// @brief Finds the first element with a standard identity.
    /// @param devices Current device descriptors.
    /// @param standard Standard control identity to find.
    /// @param device Device to search; an invalid ID searches connected devices, while a valid ID also searches disconnected devices.
    /// @return Runtime element identity, or no value when absent.
    [[nodiscard]] inline std::optional<Types::ElementId> findStandard(
        std::span<const Types::DeviceInfo> devices,
        Types::StandardId standard,
        Types::DeviceId device = {}) noexcept
    {
        if (!standard.isValid())
        {
            return std::nullopt;
        }
        const Types::ElementInfo *element = findElement(devices, Types::ElementSelector{.standard = standard}, device);
        return element == nullptr ? std::nullopt : std::optional<Types::ElementId>{element->id};
    }

    /// @brief Finds the first element with a HID identity.
    /// @param devices Current device descriptors.
    /// @param hid HID control identity to find.
    /// @param device Device to search; an invalid ID searches connected devices, while a valid ID also searches disconnected devices.
    /// @return Runtime element identity, or no value when absent.
    [[nodiscard]] inline std::optional<Types::ElementId> findHid(
        std::span<const Types::DeviceInfo> devices,
        Types::HidControlId hid,
        Types::DeviceId device = {}) noexcept
    {
        if (!hid.isValid())
        {
            return std::nullopt;
        }
        const Types::ElementInfo *element = findElement(devices, Types::ElementSelector{.hid = hid}, device);
        return element == nullptr ? std::nullopt : std::optional<Types::ElementId>{element->id};
    }
} // namespace GameWIP::Input::Query
