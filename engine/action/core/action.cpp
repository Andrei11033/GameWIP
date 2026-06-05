#include "action/action.h"

namespace GameWIP::Action
{
    ActionSettings makeButtonSettings()
    {
        return ActionSettings{.kind = ActionKind::Button};
    }

    ActionSettings makeAxis1DSettings(
        DeadzoneMode deadzoneMode,
        float innerDeadzone,
        float outerDeadzone,
        float sensitivity,
        bool invert,
        bool clampValue,
        float activationThreshold)
    {
        return ActionSettings{
            .kind = ActionKind::Axis1D,
            .clampValue = clampValue,
            .deadzoneMode = deadzoneMode,
            .innerDeadzone = innerDeadzone,
            .outerDeadzone = outerDeadzone,
            .sensitivity = sensitivity,
            .invert = invert,
            .activationThreshold = activationThreshold};
    }

    ActionSettings makeAxis2DSettings(
        DeadzoneMode deadzoneMode,
        float innerDeadzone,
        float outerDeadzone,
        float sensitivity,
        bool invertX,
        bool invertY,
        bool clampValue,
        bool normalizeDiagonal,
        float activationThreshold)
    {
        return ActionSettings{
            .kind = ActionKind::Axis2D,
            .clampValue = clampValue,
            .normalizeDiagonal = normalizeDiagonal,
            .deadzoneMode = deadzoneMode,
            .innerDeadzone = innerDeadzone,
            .outerDeadzone = outerDeadzone,
            .sensitivity = sensitivity,
            .invertX = invertX,
            .invertY = invertY,
            .activationThreshold = activationThreshold};
    }

    ActionSettings makeMovementSettings()
    {
        return makeAxis2DSettings(DeadzoneMode::None, 0.0f, 1.0f, 1.0f, false, false, true, true, 0.5f);
    }

    ActionSettings makeMouseLookSettings(float sensitivity, bool invertX, bool invertY)
    {
        return makeAxis2DSettings(DeadzoneMode::None, 0.0f, 1.0f, sensitivity, invertX, invertY, false, false, 0.0f);
    }

    ActionSettings makeGamepadStickSettings(float innerDeadzone, float outerDeadzone, float sensitivity, bool invertX, bool invertY)
    {
        return makeAxis2DSettings(DeadzoneMode::Radial, innerDeadzone, outerDeadzone, sensitivity, invertX, invertY, true, false, innerDeadzone);
    }

    ActionSettings makeGamepadTriggerSettings(float innerDeadzone, float outerDeadzone, float sensitivity, bool invert)
    {
        return makeAxis1DSettings(DeadzoneMode::Axial, innerDeadzone, outerDeadzone, sensitivity, invert, true, innerDeadzone);
    }
} // namespace GameWIP::Action
