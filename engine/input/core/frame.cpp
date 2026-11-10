/// @file frame.cpp
/// @brief Implements coherent Input frame queries.

#include "input/frame.h"

#include "input/internal/value.h"

namespace GameWIP::Input::Types
{
    const ElementState *Frame::state(ElementId id) const noexcept
    {
        if(!id.isValid() || id.slot == 0)
        {
            return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(id.slot - 1);
        return index < states_.size() && states_[index].id == id ? &states_[index] : nullptr;
    }

    const Value *Frame::value(ElementId id) const noexcept
    {
        const ElementState *current = state(id);
        return current == nullptr ? nullptr : &current->value;
    }

    std::optional<Scalar> Frame::component(ComponentId id) const noexcept
    {
        if(id.index >= 7)
        {
            return std::nullopt;
        }

        const Value *current = value(id.element);
        return current == nullptr ? std::nullopt : Internal::component(*current, id.index);
    }

    bool Frame::isDown(ElementId id) const noexcept
    {
        const ElementState *current = state(id);
        if(current == nullptr || current->validity != ValueValidity::Valid)
        {
            return false;
        }
        const bool *pressed = std::get_if<bool>(&current->value);
        return pressed != nullptr && *pressed;
    }

    bool Frame::wasPressed(ElementId id) const noexcept
    {
        return pressCount(id) != 0;
    }

    bool Frame::wasReleased(ElementId id) const noexcept
    {
        return releaseCount(id) != 0;
    }

    std::uint32_t Frame::pressCount(ElementId id) const noexcept
    {
        const ElementState *current = state(id);
        return current == nullptr ? 0 : current->becameTrueCount;
    }

    std::uint32_t Frame::releaseCount(ElementId id) const noexcept
    {
        const ElementState *current = state(id);
        return current == nullptr ? 0 : current->becameFalseCount;
    }
} // namespace GameWIP::Input::Types
