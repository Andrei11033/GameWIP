/// @file frame.cpp
/// @brief Implements coherent Input frame queries.

#include "input/frame.h"

#include "input/internal/value.h"

#include <cmath>
#include <limits>
#include <new>
#include <type_traits>
#include <variant>

namespace GameWIP::Input::Types
{
    namespace
    {
        [[nodiscard]] std::optional<Scalar> subtract(const Scalar &current, const Scalar &previous) noexcept
        {
            return std::visit(
                [](const auto &latest, const auto &baseline) -> std::optional<Scalar>
                {
                    using Latest = std::remove_cvref_t<decltype(latest)>;
                    using Baseline = std::remove_cvref_t<decltype(baseline)>;
                    if constexpr(!std::is_same_v<Latest, Baseline>)
                    {
                        return std::nullopt;
                    }
                    else if constexpr(std::is_same_v<Latest, std::int64_t>)
                    {
                        if((baseline > 0 && latest < std::numeric_limits<std::int64_t>::lowest() + baseline) ||
                           (baseline < 0 && latest > std::numeric_limits<std::int64_t>::max() + baseline))
                        {
                            return std::nullopt;
                        }

                        return Scalar{latest - baseline};
                    }
                    else if constexpr(std::is_same_v<Latest, std::uint64_t>)
                    {
                        if(latest >= baseline)
                        {
                            return Scalar{latest - baseline};
                        }

                        const std::uint64_t magnitude = baseline - latest;
                        constexpr std::uint64_t minimumMagnitude = std::uint64_t{1} << 63U;
                        if(magnitude > minimumMagnitude)
                        {
                            return std::nullopt;
                        }

                        if(magnitude == minimumMagnitude)
                        {
                            return Scalar{std::numeric_limits<std::int64_t>::lowest()};
                        }

                        return Scalar{-static_cast<std::int64_t>(magnitude)};
                    }
                    else
                    {
                        const Latest delta = latest - baseline;
                        return std::isfinite(delta) ? std::optional<Scalar>{Scalar{delta}} : std::nullopt;
                    }
                },
                current,
                previous);
        }

        [[nodiscard]] bool nonzero(const Scalar &value) noexcept
        {
            return std::visit(
                [](const auto &component)
                {
                    return component != 0;
                },
                value);
        }

        [[nodiscard]] Scalar zeroLike(const Scalar &value) noexcept
        {
            return std::visit(
                [](const auto &component) -> Scalar
                {
                    using Component = std::remove_cvref_t<decltype(component)>;
                    return Component{};
                },
                value);
        }
    } // namespace

    void Frame::reserveChangeSummaryStorage(std::span<const ElementState> states)
    {
        if(states.size() == std::numeric_limits<std::size_t>::max())
        {
            throw std::bad_alloc{};
        }

        std::size_t componentTotal = 0;
        for(const ElementState &state : states)
        {
            const std::size_t count = Internal::componentCount(Internal::valueKind(state.value));
            if(count > std::numeric_limits<std::size_t>::max() - componentTotal)
            {
                throw std::bad_alloc{};
            }

            componentTotal += count;
        }

        componentOffsets_.reserve(states.size() + 1);
        elementChanges_.reserve(states.size());
        componentChanges_.reserve(componentTotal);
    }

    void Frame::buildChangeSummaries(std::span<const ElementState> states, std::span<const Event> events) noexcept
    {
        // Size the storage reserved before capture data is moved.
        std::size_t componentTotal = 0;
        componentOffsets_.resize(states.size() + 1);
        elementChanges_.resize(states.size());

        for(const ElementState &state : states)
        {
            componentTotal += Internal::componentCount(Internal::valueKind(state.value));
        }
        componentChanges_.resize(componentTotal);

        // Prepare per-element component ranges and known zero deltas.
        std::size_t componentOffset = 0;
        for(std::size_t index = 0; index < states.size(); ++index)
        {
            const ElementState &state = states[index];
            const ValueKind kind = Internal::valueKind(state.value);
            const std::size_t componentTotalForState = Internal::componentCount(kind);
            const std::size_t offset = componentOffset;
            componentOffsets_[index] = offset;
            componentOffset += componentTotalForState;
            componentOffsets_[index + 1] = componentOffset;
            elementChanges_[index] = {};

            for(std::size_t componentIndex = 0; componentIndex < componentTotalForState; ++componentIndex)
            {
                ComponentChangeSummary &summary = componentChanges_[offset + componentIndex];
                summary = {};

                if(state.validity != ValueValidity::Valid)
                {
                    continue;
                }

                if(state.reporting == ReportingMode::Relative)
                {
                    const std::optional<Scalar> current = Internal::component(state.value, componentIndex);
                    if(current.has_value())
                    {
                        summary.delta = *current;
                        summary.deltaKnown = true;
                    }
                    continue;
                }

                const std::optional<Scalar> current = Internal::component(state.value, componentIndex);
                if(current.has_value())
                {
                    summary.delta = zeroLike(*current);
                    summary.deltaKnown = true;
                }
            }
        }

        // Count only value changes present in retained events.
        for(const Event &event : events)
        {
            if(event.kind != EventKind::ElementChanged)
            {
                continue;
            }

            const ElementEvent *elementEvent = std::get_if<ElementEvent>(&event.data);
            if(elementEvent == nullptr || !elementEvent->id.isValid() || elementEvent->id.slot == 0)
            {
                continue;
            }

            const std::size_t stateIndex = static_cast<std::size_t>(elementEvent->id.slot - 1);
            if(stateIndex >= states.size() || states[stateIndex].id != elementEvent->id)
            {
                continue;
            }

            const ElementState &state = states[stateIndex];
            const ValueKind kind = Internal::valueKind(state.value);
            const std::size_t componentTotalForState = Internal::componentCount(kind);
            const std::size_t offset = componentOffsets_[stateIndex];
            bool elementChanged = false;

            if(state.reporting == ReportingMode::Absolute)
            {
                for(std::size_t componentIndex = 0; componentIndex < componentTotalForState; ++componentIndex)
                {
                    ComponentChangeSummary &summary = componentChanges_[offset + componentIndex];
                    if(!summary.baselineCaptured)
                    {
                        summary.baselineCaptured = true;
                        const std::optional<Scalar> baseline =
                            elementEvent->previousKnown ? Internal::component(elementEvent->previousValue, componentIndex) : std::nullopt;
                        if(baseline.has_value())
                        {
                            summary.delta = *baseline;
                            summary.deltaKnown = true;
                        }
                        else
                        {
                            summary.deltaKnown = false;
                        }
                    }

                    if(!elementEvent->previousKnown)
                    {
                        continue;
                    }

                    const std::optional<Scalar> previous = Internal::component(elementEvent->previousValue, componentIndex);
                    const std::optional<Scalar> current = Internal::component(elementEvent->value, componentIndex);
                    if(previous.has_value() && current.has_value() && *previous != *current)
                    {
                        ++summary.changeCount;
                        elementChanged = true;
                    }
                }

                if(elementEvent->previousKnown && elementEvent->value != elementEvent->previousValue)
                {
                    elementChanged = true;
                }
            }
            else
            {
                const Value zero = Internal::zeroValue(kind);
                elementChanged = elementEvent->value != zero;

                for(std::size_t componentIndex = 0; componentIndex < componentTotalForState; ++componentIndex)
                {
                    ComponentChangeSummary &summary = componentChanges_[offset + componentIndex];
                    const std::optional<Scalar> delta = Internal::component(elementEvent->value, componentIndex);
                    if(delta.has_value() && nonzero(*delta))
                    {
                        ++summary.changeCount;
                    }
                }
            }

            if(elementChanged)
            {
                ++elementChanges_[stateIndex].changeCount;
            }
        }

        // Resolve absolute differences against the captured state.
        for(std::size_t index = 0; index < states.size(); ++index)
        {
            const ElementState &state = states[index];
            const std::size_t componentTotalForState = Internal::componentCount(Internal::valueKind(state.value));
            const std::size_t offset = componentOffsets_[index];

            for(std::size_t componentIndex = 0; componentIndex < componentTotalForState; ++componentIndex)
            {
                ComponentChangeSummary &summary = componentChanges_[offset + componentIndex];
                if(state.validity != ValueValidity::Valid)
                {
                    summary.deltaKnown = false;
                    continue;
                }

                const std::optional<Scalar> current = Internal::component(state.value, componentIndex);
                if(!current.has_value())
                {
                    summary.deltaKnown = false;
                    continue;
                }

                if(state.reporting == ReportingMode::Relative)
                {
                    summary.delta = *current;
                    summary.deltaKnown = true;
                }
                else if(summary.baselineCaptured && summary.deltaKnown)
                {
                    const std::optional<Scalar> delta = subtract(*current, summary.delta);
                    if(delta.has_value())
                    {
                        summary.delta = *delta;
                    }
                    else
                    {
                        summary.deltaKnown = false;
                    }
                }
            }
        }
    }

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
        const Value *current = value(id.element);
        return current == nullptr ? std::nullopt : Internal::component(*current, id.index);
    }

    bool Frame::wasChanged(ElementId id) const noexcept
    {
        return changeCount(id) != 0;
    }

    std::uint32_t Frame::changeCount(ElementId id) const noexcept
    {
        if(state(id) == nullptr || id.slot == 0)
        {
            return 0;
        }

        const std::size_t index = static_cast<std::size_t>(id.slot - 1);
        return index < elementChanges_.size() ? elementChanges_[index].changeCount : 0;
    }

    bool Frame::wasChanged(ComponentId id) const noexcept
    {
        return changeCount(id) != 0;
    }

    std::uint32_t Frame::changeCount(ComponentId id) const noexcept
    {
        const ComponentChangeSummary *summary = componentChange(id);
        return summary == nullptr ? 0 : summary->changeCount;
    }

    std::optional<Scalar> Frame::componentDelta(ComponentId id) const noexcept
    {
        const ComponentChangeSummary *summary = componentChange(id);
        return summary == nullptr || !summary->deltaKnown ? std::nullopt : std::optional<Scalar>{summary->delta};
    }

    const Frame::ComponentChangeSummary *Frame::componentChange(ComponentId id) const noexcept
    {
        if(state(id.element) == nullptr || id.element.slot == 0)
        {
            return nullptr;
        }

        const std::size_t stateIndex = static_cast<std::size_t>(id.element.slot - 1);
        if(stateIndex + 1 >= componentOffsets_.size())
        {
            return nullptr;
        }

        const std::size_t offset = componentOffsets_[stateIndex];
        const std::size_t end = componentOffsets_[stateIndex + 1];
        if(id.index >= end - offset)
        {
            return nullptr;
        }

        return &componentChanges_[offset + id.index];
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
