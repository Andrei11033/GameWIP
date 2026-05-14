#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Action
{
    namespace Internal
    {
        inline bool isKeyboardNoneControl(Input::InputControl control)
        {
            return control.deviceType == Input::InputDeviceType::Keyboard &&
                   control.deviceIndex == 0 &&
                   control.controlType == Input::InputControlType::Button &&
                   control.controlCode == 0;
        }

        inline bool isValidControl(Input::InputControl control)
        {
            return !isKeyboardNoneControl(control);
        }

        inline bool isButtonControl(Input::InputControl control)
        {
            return control.controlType == Input::InputControlType::Button;
        }

        inline bool isAxisControl(Input::InputControl control)
        {
            return control.controlType == Input::InputControlType::Axis;
        }

        inline bool isWheelControl(Input::InputControl control)
        {
            return control.controlType == Input::InputControlType::Wheel;
        }

        inline bool containsControl(std::span<const Input::InputControl> controls, Input::InputControl control)
        {
            return std::find(controls.begin(), controls.end(), control) != controls.end();
        }

        inline bool containsControl(const std::vector<Input::InputControl> &controls, Input::InputControl control)
        {
            return std::find(controls.begin(), controls.end(), control) != controls.end();
        }

        inline bool isFinite(float value)
        {
            return std::isfinite(value);
        }

        inline bool isKeyboardModifier(Input::InputControl control)
        {
            if (control.deviceType != Input::InputDeviceType::Keyboard ||
                control.deviceIndex != 0 ||
                control.controlType != Input::InputControlType::Button)
            {
                return false;
            }

            return control.controlCode == Input::KeyboardControlCode::LeftControl ||
                   control.controlCode == Input::KeyboardControlCode::RightControl ||
                   control.controlCode == Input::KeyboardControlCode::LeftShift ||
                   control.controlCode == Input::KeyboardControlCode::RightShift ||
                   control.controlCode == Input::KeyboardControlCode::LeftAlt ||
                   control.controlCode == Input::KeyboardControlCode::RightAlt ||
                   control.controlCode == Input::KeyboardControlCode::LeftSuper ||
                   control.controlCode == Input::KeyboardControlCode::RightSuper;
        }

        inline bool isCaptureActivation(const Input::InputActivation &activation)
        {
            switch (activation.activationType)
            {
            case Input::InputActivationType::ButtonPressed:
            case Input::InputActivationType::AxisPositive:
            case Input::InputActivationType::AxisNegative:
            case Input::InputActivationType::WheelPositive:
            case Input::InputActivationType::WheelNegative:
                return true;
            case Input::InputActivationType::ButtonReleased:
                return false;
            }

            return false;
        }

        inline bool isButtonPressActivation(const Input::InputActivation &activation)
        {
            return activation.activationType == Input::InputActivationType::ButtonPressed;
        }

        inline bool isButtonReleaseActivation(const Input::InputActivation &activation)
        {
            return activation.activationType == Input::InputActivationType::ButtonReleased;
        }

        inline void addUniqueControl(std::vector<Input::InputControl> &controls, Input::InputControl control)
        {
            if (!containsControl(controls, control))
            {
                controls.push_back(control);
            }
        }

        inline bool isValidActionSettings(const ActionSettings &settings)
        {
            return isFinite(settings.innerDeadzone) &&
                   isFinite(settings.outerDeadzone) &&
                   isFinite(settings.sensitivity) &&
                   isFinite(settings.curveExponent) &&
                   isFinite(settings.activationThreshold) &&
                   settings.innerDeadzone >= 0.0f &&
                   settings.innerDeadzone <= 1.0f &&
                   settings.outerDeadzone >= settings.innerDeadzone &&
                   settings.outerDeadzone <= 1.0f &&
                   settings.sensitivity >= 0.0f &&
                   settings.curveExponent > 0.0f &&
                   settings.activationThreshold >= 0.0f;
        }

        template <typename ActionEnum>
        bool isBindingCompatibleWithActionKind(ActionKind kind, const ActionBinding<ActionEnum> &binding)
        {
            switch (kind)
            {
            case ActionKind::Button:
                return binding.gesture.trigger != ActionTrigger::Value &&
                       binding.valueMapping.component == ActionComponent::Scalar;
            case ActionKind::Axis1D:
                return binding.gesture.trigger == ActionTrigger::Value &&
                       binding.valueMapping.component == ActionComponent::Scalar;
            case ActionKind::Axis2D:
                return binding.gesture.trigger == ActionTrigger::Value &&
                       binding.valueMapping.component != ActionComponent::Scalar;
            }

            return false;
        }

        inline bool shouldCaptureModifier(
            Input::InputControl modifier,
            Input::InputControl primaryControl,
            RebindModifierMode modifierMode,
            std::span<const Input::InputControl> cancelControls,
            std::span<const Input::InputControl> ignoredControls)
        {
            if (modifierMode == RebindModifierMode::None ||
                modifier == primaryControl ||
                !isButtonControl(modifier) ||
                containsControl(cancelControls, modifier) ||
                containsControl(ignoredControls, modifier))
            {
                return false;
            }

            if (modifierMode == RebindModifierMode::KeyboardModifiersOnly)
            {
                return isKeyboardModifier(modifier);
            }

            return true;
        }

        inline bool hasDuplicateControls(const std::vector<Input::InputControl> &controls)
        {
            for (std::size_t controlIndex = 0; controlIndex < controls.size(); ++controlIndex)
            {
                for (std::size_t compareIndex = controlIndex + 1; compareIndex < controls.size(); ++compareIndex)
                {
                    if (controls[controlIndex] == controls[compareIndex])
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        inline bool haveSameModifierSet(const std::vector<Input::InputControl> &left, const std::vector<Input::InputControl> &right)
        {
            if (left.size() != right.size())
            {
                return false;
            }

            for (Input::InputControl modifier : left)
            {
                if (!containsControl(right, modifier))
                {
                    return false;
                }
            }

            return true;
        }

        inline bool areSameCombo(const ActionCombo &left, const ActionCombo &right)
        {
            return left.primaryControl == right.primaryControl &&
                   left.activationMode == right.activationMode &&
                   haveSameModifierSet(left.modifiers, right.modifiers);
        }

        inline bool areSameGesture(const ActionGesture &left, const ActionGesture &right)
        {
            return left.trigger == right.trigger &&
                   left.holdSeconds == right.holdSeconds &&
                   left.doubleTapSeconds == right.doubleTapSeconds;
        }

        inline bool areSameValueMapping(const ActionValueMapping &left, const ActionValueMapping &right)
        {
            return left.component == right.component &&
                   left.scale == right.scale &&
                   left.threshold == right.threshold;
        }

        template <typename ActionEnum>
        bool areSameBinding(const ActionBinding<ActionEnum> &left, const ActionBinding<ActionEnum> &right)
        {
            return left.action == right.action &&
                   areSameCombo(left.combo, right.combo) &&
                   areSameGesture(left.gesture, right.gesture) &&
                   areSameValueMapping(left.valueMapping, right.valueMapping);
        }

        template <typename ActionEnum>
        bool hasBindingConflict(const ActionBinding<ActionEnum> &left, const ActionBinding<ActionEnum> &right)
        {
            return left.action != right.action &&
                   areSameCombo(left.combo, right.combo) &&
                   areSameGesture(left.gesture, right.gesture);
        }

        inline bool areModifiersDown(const Input::InputState &inputState, const ActionCombo &combo)
        {
            for (Input::InputControl modifier : combo.modifiers)
            {
                if (!inputState.isButtonDown(modifier))
                {
                    return false;
                }
            }

            return true;
        }

        inline bool isComboDown(const Input::InputState &inputState, const ActionCombo &combo)
        {
            return inputState.isButtonDown(combo.primaryControl) && areModifiersDown(inputState, combo);
        }

        inline bool wasPrimaryPressed(const Input::InputState &inputState, const ActionCombo &combo)
        {
            return inputState.wasButtonPressed(combo.primaryControl) && areModifiersDown(inputState, combo);
        }

        inline bool wasPrimaryReleased(const Input::InputState &inputState, const ActionCombo &combo)
        {
            return inputState.wasButtonReleased(combo.primaryControl);
        }

        inline float absoluteValue(float value)
        {
            return value < 0.0f ? -value : value;
        }

        inline float clamp(float value, float minimum, float maximum)
        {
            if (value < minimum)
            {
                return minimum;
            }

            if (value > maximum)
            {
                return maximum;
            }

            return value;
        }

        inline float sign(float value)
        {
            return value < 0.0f ? -1.0f : 1.0f;
        }

        inline float getVectorLength(float x, float y)
        {
            return std::sqrt((x * x) + (y * y));
        }

        inline bool passesThreshold(float value, float threshold)
        {
            return absoluteValue(value) > threshold;
        }

        inline float applyDeadzone1D(float value, float innerDeadzone, float outerDeadzone)
        {
            const float magnitude = absoluteValue(value);
            const float inner = clamp(innerDeadzone, 0.0f, 1.0f);
            const float outer = clamp(outerDeadzone, inner, 1.0f);

            if (magnitude <= inner)
            {
                return 0.0f;
            }

            if (magnitude >= outer)
            {
                return sign(value);
            }

            const float remappedMagnitude = (magnitude - inner) / (outer - inner);
            return sign(value) * remappedMagnitude;
        }

        inline void applyAxialDeadzone2D(float &x, float &y, float innerDeadzone, float outerDeadzone)
        {
            x = applyDeadzone1D(x, innerDeadzone, outerDeadzone);
            y = applyDeadzone1D(y, innerDeadzone, outerDeadzone);
        }

        inline void applyRadialDeadzone2D(float &x, float &y, float innerDeadzone, float outerDeadzone)
        {
            const float length = getVectorLength(x, y);
            const float inner = clamp(innerDeadzone, 0.0f, 1.0f);
            const float outer = clamp(outerDeadzone, inner, 1.0f);

            if (length <= inner || length == 0.0f)
            {
                x = 0.0f;
                y = 0.0f;
                return;
            }

            if (length >= outer)
            {
                x /= length;
                y /= length;
                return;
            }

            const float remappedLength = (length - inner) / (outer - inner);
            const float scale = remappedLength / length;
            x *= scale;
            y *= scale;
        }

        inline float applyCurve1D(float value, float curveExponent)
        {
            const float exponent = curveExponent > 0.0f ? curveExponent : 1.0f;
            return sign(value) * std::pow(absoluteValue(value), exponent);
        }

        inline void applyCurve2D(float &x, float &y, float curveExponent)
        {
            const float length = getVectorLength(x, y);
            if (length == 0.0f)
            {
                return;
            }

            const float exponent = curveExponent > 0.0f ? curveExponent : 1.0f;
            const float curvedLength = std::pow(length, exponent);
            const float scale = curvedLength / length;
            x *= scale;
            y *= scale;
        }

        inline void clampVectorLength(float &x, float &y, float maximumLength)
        {
            const float length = getVectorLength(x, y);
            if (length <= maximumLength || length == 0.0f)
            {
                return;
            }

            const float scale = maximumLength / length;
            x *= scale;
            y *= scale;
        }

        inline bool isMouseAxis(Input::InputControl control, Input::MouseAxis axis)
        {
            return control.deviceType == Input::InputDeviceType::Mouse &&
                   control.deviceIndex == 0 &&
                   control.controlType == Input::InputControlType::Axis &&
                   control.controlCode == static_cast<Input::ControlCode>(axis);
        }

        inline float getMouseAxisValue(const Input::InputState &inputState, Input::InputControl control)
        {
            if (isMouseAxis(control, Input::MouseAxis::DeltaX))
            {
                return static_cast<float>(inputState.getMouseDeltaX());
            }

            if (isMouseAxis(control, Input::MouseAxis::DeltaY))
            {
                return static_cast<float>(inputState.getMouseDeltaY());
            }

            return 0.0f;
        }

        inline float getControlValue(const Input::InputState &inputState, Input::InputControl control)
        {
            switch (control.controlType)
            {
            case Input::InputControlType::Button:
                return inputState.isButtonDown(control) ? 1.0f : 0.0f;
            case Input::InputControlType::Axis:
                if (control.deviceType == Input::InputDeviceType::Mouse)
                {
                    return getMouseAxisValue(inputState, control);
                }

                return inputState.getAxis(control);
            case Input::InputControlType::Wheel:
                return inputState.getWheelDelta(control);
            }

            return 0.0f;
        }
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum>::ActionBindingBuilder(ActionMap<ActionEnum> &actionMap, ActionEnum action)
        : actionMap(actionMap)
    {
        binding.action = action;
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::on(Input::InputControl primaryControl)
    {
        binding.combo.primaryControl = primaryControl;
        return *this;
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::withModifier(Input::InputControl modifier)
    {
        binding.combo.modifiers.push_back(modifier);
        return *this;
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::withModifiers(std::span<const Input::InputControl> modifiers)
    {
        for (Input::InputControl modifier : modifiers)
        {
            binding.combo.modifiers.push_back(modifier);
        }

        return *this;
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::primaryLast()
    {
        binding.combo.activationMode = ComboActivationMode::PrimaryLast;
        return *this;
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::anyOrder()
    {
        binding.combo.activationMode = ComboActivationMode::AnyOrder;
        return *this;
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::pressed()
    {
        return add(ActionTrigger::Pressed, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::released()
    {
        return add(ActionTrigger::Released, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::down()
    {
        return add(ActionTrigger::Down, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::tap()
    {
        return add(ActionTrigger::Tap, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::hold(float seconds)
    {
        return add(ActionTrigger::Hold, ActionComponent::Scalar, 1.0f, 0.0f, seconds, 0.0f);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::doubleTap(float seconds)
    {
        return add(ActionTrigger::DoubleTap, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, seconds);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::value(ActionComponent component, float scale, float threshold)
    {
        return add(ActionTrigger::Value, component, scale, threshold, 0.0f, 0.0f);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::axis1D(float scale, float threshold)
    {
        return value(ActionComponent::Scalar, scale, threshold);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::axis2D(ActionComponent component, float scale, float threshold)
    {
        if (component == ActionComponent::Scalar)
        {
            return ActionResult::InvalidBinding;
        }

        return value(component, scale, threshold);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::add(ActionTrigger trigger, ActionComponent component, float scale, float threshold, float holdSeconds, float doubleTapSeconds)
    {
        binding.gesture.trigger = trigger;
        binding.gesture.holdSeconds = holdSeconds;
        binding.gesture.doubleTapSeconds = doubleTapSeconds;
        binding.valueMapping.component = component;
        binding.valueMapping.scale = scale;
        binding.valueMapping.threshold = threshold;

        return actionMap.addBinding(binding);
    }

    template <typename ActionEnum>
    ActionMap<ActionEnum>::ActionMap(ActionEnum actionCount)
    {
        resize(actionCount);
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::resize(ActionEnum actionCount)
    {
        const std::size_t count = static_cast<std::size_t>(actionCount);
        actionSettings.resize(count);
        actionStates.resize(count);
    }

    template <typename ActionEnum>
    std::size_t ActionMap<ActionEnum>::getActionCount() const
    {
        return actionSettings.size();
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::isValidAction(ActionEnum action) const
    {
        return getActionIndex(action) < actionSettings.size();
    }

    template <typename ActionEnum>
    ActionResult ActionMap<ActionEnum>::defineAction(ActionEnum action, ActionKind kind)
    {
        if (!isValidAction(action))
        {
            return ActionResult::InvalidAction;
        }

        actionSettings[getActionIndex(action)].kind = kind;
        return ActionResult::Success;
    }

    template <typename ActionEnum>
    ActionResult ActionMap<ActionEnum>::setActionSettings(ActionEnum action, const ActionSettings &settings)
    {
        if (!isValidAction(action))
        {
            return ActionResult::InvalidAction;
        }

        if (!Internal::isValidActionSettings(settings))
        {
            return ActionResult::InvalidSettings;
        }

        actionSettings[getActionIndex(action)] = settings;
        return ActionResult::Success;
    }

    template <typename ActionEnum>
    const ActionSettings *ActionMap<ActionEnum>::getActionSettings(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return nullptr;
        }

        return &actionSettings[getActionIndex(action)];
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::advanceFrame()
    {
        for (ActionState &actionState : actionStates)
        {
            actionState.pressed = false;
            actionState.released = false;
            actionState.down = false;
            actionState.value = {};
        }

        textInputUtf8Snapshot.clear();
        mouseDeltaXSnapshot = 0;
        mouseDeltaYSnapshot = 0;
        verticalWheelDeltaSnapshot = 0.0f;
        horizontalWheelDeltaSnapshot = 0.0f;
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::evaluate(const Input::InputState &inputState, float deltaSeconds)
    {
        const float frameSeconds = deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
        copyInputSnapshot(inputState);

        for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
        {
            const ActionBinding<ActionEnum> &binding = bindings[bindingIndex];
            RuntimeBindingState &bindingState = bindingStates[bindingIndex];

            if (!isValidAction(binding.action))
            {
                continue;
            }

            const bool wasBindingActive = bindingState.active;
            const bool comboDown = Internal::isComboDown(inputState, binding.combo);
            const bool startsThisFrame = binding.combo.activationMode == ComboActivationMode::AnyOrder
                                             ? comboDown && !wasBindingActive
                                             : Internal::wasPrimaryPressed(inputState, binding.combo);
            const bool nextBindingActive = comboDown && (binding.combo.activationMode == ComboActivationMode::AnyOrder
                                                             ? true
                                                             : wasBindingActive || startsThisFrame);
            const bool stopsThisFrame = wasBindingActive && !nextBindingActive;
            bool nextRuntimeActive = nextBindingActive;
            bool bindingDown = false;
            bool bindingContributesValue = false;
            bool bindingPressed = false;
            bool bindingReleased = false;
            float bindingValue = 1.0f;

            switch (binding.gesture.trigger)
            {
            case ActionTrigger::Pressed:
                bindingPressed = startsThisFrame;
                bindingDown = bindingPressed;
                break;

            case ActionTrigger::Released:
                bindingReleased = stopsThisFrame;
                bindingDown = bindingReleased;
                break;

            case ActionTrigger::Down:
                bindingDown = nextBindingActive;
                bindingPressed = startsThisFrame;
                bindingReleased = stopsThisFrame;
                break;

            case ActionTrigger::Tap:
                if (nextBindingActive)
                {
                    bindingState.heldSeconds += frameSeconds;
                }

                if (stopsThisFrame && !hasMatchingHoldFired(bindingIndex))
                {
                    bindingPressed = true;
                    bindingDown = true;
                }

                if (!nextBindingActive)
                {
                    bindingState.heldSeconds = 0.0f;
                }
                break;

            case ActionTrigger::Hold:
                if (startsThisFrame)
                {
                    bindingState.heldSeconds = 0.0f;
                    bindingState.holdFired = false;
                }

                if (nextBindingActive)
                {
                    bindingState.heldSeconds += frameSeconds;
                    if (!bindingState.holdFired && bindingState.heldSeconds >= binding.gesture.holdSeconds)
                    {
                        bindingState.holdFired = true;
                        bindingPressed = true;
                    }

                    bindingDown = bindingState.holdFired;
                }
                else
                {
                    if (stopsThisFrame && bindingState.holdFired)
                    {
                        bindingReleased = true;
                    }

                    bindingState.heldSeconds = 0.0f;
                }
                break;

            case ActionTrigger::DoubleTap:
                if (bindingState.waitingForSecondTap)
                {
                    bindingState.timeSinceLastTap += frameSeconds;

                    if (!Internal::areModifiersDown(inputState, binding.combo) ||
                        bindingState.timeSinceLastTap > binding.gesture.doubleTapSeconds)
                    {
                        bindingState.waitingForSecondTap = false;
                        bindingState.timeSinceLastTap = 0.0f;
                    }
                }

                if (stopsThisFrame)
                {
                    if (bindingState.waitingForSecondTap)
                    {
                        bindingPressed = true;
                        bindingDown = true;
                        bindingState.waitingForSecondTap = false;
                        bindingState.timeSinceLastTap = 0.0f;
                    }
                    else
                    {
                        bindingState.waitingForSecondTap = true;
                        bindingState.timeSinceLastTap = 0.0f;
                    }
                }
                break;

            case ActionTrigger::Value:
            {
                const float rawValue = Internal::getControlValue(inputState, binding.combo.primaryControl);
                const bool valueActive = Internal::areModifiersDown(inputState, binding.combo) &&
                                         Internal::passesThreshold(rawValue, binding.valueMapping.threshold);

                if (Internal::isButtonControl(binding.combo.primaryControl))
                {
                    bindingContributesValue = nextBindingActive;
                    bindingValue = 1.0f;
                    nextRuntimeActive = nextBindingActive;
                }
                else
                {
                    bindingContributesValue = valueActive;
                    bindingValue = rawValue;
                    nextRuntimeActive = valueActive;
                }

                break;
            }

            default:
                break;
            }

            if (bindingPressed)
            {
                actionStates[getActionIndex(binding.action)].pressed = true;
            }

            if (bindingReleased)
            {
                actionStates[getActionIndex(binding.action)].released = true;
            }

            if (bindingDown)
            {
                applyBindingDown(binding.action, true);
                addBindingValue(binding.action, binding.valueMapping, bindingValue);
            }
            else if (bindingContributesValue)
            {
                addBindingValue(binding.action, binding.valueMapping, bindingValue);
            }

            bindingState.active = nextRuntimeActive;
        }

        processActionValues();
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::hasTextInput() const
    {
        return !textInputUtf8Snapshot.empty();
    }

    template <typename ActionEnum>
    std::string_view ActionMap<ActionEnum>::getTextInputUtf8() const
    {
        return textInputUtf8Snapshot;
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::hasMousePosition() const
    {
        return mousePositionKnownSnapshot;
    }

    template <typename ActionEnum>
    int ActionMap<ActionEnum>::getMouseX() const
    {
        return mouseXSnapshot;
    }

    template <typename ActionEnum>
    int ActionMap<ActionEnum>::getMouseY() const
    {
        return mouseYSnapshot;
    }

    template <typename ActionEnum>
    int ActionMap<ActionEnum>::getMouseDeltaX() const
    {
        return mouseDeltaXSnapshot;
    }

    template <typename ActionEnum>
    int ActionMap<ActionEnum>::getMouseDeltaY() const
    {
        return mouseDeltaYSnapshot;
    }

    template <typename ActionEnum>
    float ActionMap<ActionEnum>::getMouseWheelDelta(Input::MouseWheel wheel) const
    {
        switch (wheel)
        {
        case Input::MouseWheel::Vertical:
            return verticalWheelDeltaSnapshot;
        case Input::MouseWheel::Horizontal:
            return horizontalWheelDeltaSnapshot;
        }

        return 0.0f;
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::isDown(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return false;
        }

        return actionStates[getActionIndex(action)].down;
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::wasPressed(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return false;
        }

        return actionStates[getActionIndex(action)].pressed;
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::wasReleased(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return false;
        }

        return actionStates[getActionIndex(action)].released;
    }

    template <typename ActionEnum>
    float ActionMap<ActionEnum>::getValue(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return 0.0f;
        }

        return actionStates[getActionIndex(action)].value.scalar;
    }

    template <typename ActionEnum>
    float ActionMap<ActionEnum>::getValueX(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return 0.0f;
        }

        return actionStates[getActionIndex(action)].value.x;
    }

    template <typename ActionEnum>
    float ActionMap<ActionEnum>::getValueY(ActionEnum action) const
    {
        if (!isValidAction(action))
        {
            return 0.0f;
        }

        return actionStates[getActionIndex(action)].value.y;
    }

    template <typename ActionEnum>
    std::span<const ActionBinding<ActionEnum>> ActionMap<ActionEnum>::getBindings() const
    {
        return bindings;
    }

    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum> ActionMap<ActionEnum>::bind(ActionEnum action)
    {
        return ActionBindingBuilder<ActionEnum>(*this, action);
    }

    template <typename ActionEnum>
    ActionResult ActionMap<ActionEnum>::addBinding(const ActionBinding<ActionEnum> &binding)
    {
        const ActionResult result = validateBinding(binding, false);
        if (result != ActionResult::Success && result != ActionResult::ConflictingBinding)
        {
            return result;
        }

        addBindingUnchecked(binding);
        return result;
    }

    template <typename ActionEnum>
    RebindResult ActionMap<ActionEnum>::beginBindingCapture(
        ActionEnum action,
        const ActionRebindOptions &options,
        ActionRebindSession<ActionEnum> &outSession) const
    {
        outSession = {};
        outSession.action = action;
        outSession.options = options;

        if (!isValidAction(action))
        {
            return RebindResult::InvalidAction;
        }

        outSession.active = true;
        return RebindResult::Collecting;
    }

    template <typename ActionEnum>
    RebindResult ActionMap<ActionEnum>::updateBindingCapture(
        const Input::InputState &inputState,
        ActionRebindSession<ActionEnum> &session,
        ActionRebindCapture<ActionEnum> &outCapture,
        std::span<const Input::InputControl> cancelControls,
        std::span<const Input::InputControl> ignoredControls) const
    {
        outCapture = {};

        if (!session.active)
        {
            return RebindResult::None;
        }

        if (!isValidAction(session.action))
        {
            session.active = false;
            return RebindResult::InvalidAction;
        }

        for (const Input::InputActivation &activation : inputState.getActivationView())
        {
            if (Internal::containsControl(cancelControls, activation.control))
            {
                session.active = false;
                return RebindResult::Canceled;
            }

            if (Internal::containsControl(ignoredControls, activation.control))
            {
                continue;
            }

            if (session.options.completionMode == RebindCompletionMode::OnActivation)
            {
                if (!Internal::isCaptureActivation(activation))
                {
                    continue;
                }

                const RebindResult result = buildRebindCapture(
                    session.action,
                    activation,
                    activation.control,
                    inputState.getCurrentButtonView(),
                    session.options,
                    outCapture,
                    cancelControls,
                    ignoredControls);

                session.active = result == RebindResult::Collecting;
                return result;
            }

            if (Internal::isCaptureActivation(activation))
            {
                if (!Internal::isValidControl(activation.control))
                {
                    session.active = false;
                    return RebindResult::InvalidControl;
                }

                if (Internal::isButtonPressActivation(activation))
                {
                    Internal::addUniqueControl(session.controls, activation.control);
                    session.primaryControl = activation.control;
                    session.activation = activation;
                    session.hasPrimaryControl = true;
                    continue;
                }

                const RebindResult result = buildRebindCapture(
                    session.action,
                    activation,
                    activation.control,
                    inputState.getCurrentButtonView(),
                    session.options,
                    outCapture,
                    cancelControls,
                    ignoredControls);

                session.active = result == RebindResult::Collecting;
                return result;
            }

            if (Internal::isButtonReleaseActivation(activation) &&
                session.hasPrimaryControl &&
                Internal::containsControl(session.controls, activation.control))
            {
                const RebindResult result = buildRebindCapture(
                    session.action,
                    session.activation,
                    session.primaryControl,
                    session.controls,
                    session.options,
                    outCapture,
                    cancelControls,
                    ignoredControls);

                session.active = result == RebindResult::Collecting;
                return result;
            }
        }

        return RebindResult::Collecting;
    }

    template <typename ActionEnum>
    RebindResult ActionMap<ActionEnum>::captureBinding(
        ActionEnum action,
        const Input::InputState &inputState,
        const ActionRebindOptions &options,
        ActionRebindCapture<ActionEnum> &outCapture,
        std::span<const Input::InputControl> cancelControls,
        std::span<const Input::InputControl> ignoredControls) const
    {
        outCapture = {};

        if (!isValidAction(action))
        {
            outCapture.result = RebindResult::InvalidAction;
            return outCapture.result;
        }

        const std::span<const Input::InputActivation> activations = inputState.getActivationView();
        for (std::size_t activationIndex = activations.size(); activationIndex > 0; --activationIndex)
        {
            const Input::InputActivation &activation = activations[activationIndex - 1];
            if (Internal::containsControl(cancelControls, activation.control))
            {
                outCapture.result = RebindResult::Canceled;
                return outCapture.result;
            }

            if (Internal::containsControl(ignoredControls, activation.control) ||
                !Internal::isCaptureActivation(activation))
            {
                continue;
            }

            return buildRebindCapture(
                action,
                activation,
                activation.control,
                inputState.getCurrentButtonView(),
                options,
                outCapture,
                cancelControls,
                ignoredControls);
        }

        outCapture.result = RebindResult::None;
        return outCapture.result;
    }

    template <typename ActionEnum>
    ActionResult ActionMap<ActionEnum>::applyCapturedBinding(const ActionRebindCapture<ActionEnum> &capture)
    {
        if (capture.result != RebindResult::Captured)
        {
            return ActionResult::InvalidBinding;
        }

        const ActionResult result = validateBinding(capture.binding, capture.replaceExistingBindings);
        if (result != ActionResult::Success && result != ActionResult::ConflictingBinding)
        {
            return result;
        }

        if (capture.replaceExistingBindings)
        {
            clearBindings(capture.binding.action);
        }

        addBindingUnchecked(capture.binding);
        return result;
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::clearBindings(ActionEnum action)
    {
        if (!isValidAction(action))
        {
            return;
        }

        for (std::size_t bindingIndex = 0; bindingIndex < bindings.size();)
        {
            if (bindings[bindingIndex].action == action)
            {
                bindings.erase(bindings.begin() + bindingIndex);
                bindingStates.erase(bindingStates.begin() + bindingIndex);
            }
            else
            {
                ++bindingIndex;
            }
        }
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::clearAllBindings()
    {
        bindings.clear();
        bindingStates.clear();
    }

    template <typename ActionEnum>
    std::size_t ActionMap<ActionEnum>::getActionIndex(ActionEnum action) const
    {
        return static_cast<std::size_t>(action);
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::copyInputSnapshot(const Input::InputState &inputState)
    {
        textInputUtf8Snapshot = inputState.getTextInputUtf8();
        mouseDeltaXSnapshot = inputState.getMouseDeltaX();
        mouseDeltaYSnapshot = inputState.getMouseDeltaY();
        mousePositionKnownSnapshot = inputState.hasMousePosition();
        mouseXSnapshot = inputState.getMouseX();
        mouseYSnapshot = inputState.getMouseY();
        verticalWheelDeltaSnapshot = inputState.getWheelDelta(Input::makeMouseWheel(Input::MouseWheel::Vertical));
        horizontalWheelDeltaSnapshot = inputState.getWheelDelta(Input::makeMouseWheel(Input::MouseWheel::Horizontal));
    }

    template <typename ActionEnum>
    ActionResult ActionMap<ActionEnum>::validateBinding(const ActionBinding<ActionEnum> &binding, bool ignoreSameActionBindings) const
    {
        if (!isValidAction(binding.action))
        {
            return ActionResult::InvalidAction;
        }

        if (!Internal::isValidControl(binding.combo.primaryControl))
        {
            return ActionResult::InvalidControl;
        }

        if (!Internal::isBindingCompatibleWithActionKind(actionSettings[getActionIndex(binding.action)].kind, binding))
        {
            return ActionResult::InvalidBinding;
        }

        if (binding.gesture.trigger != ActionTrigger::Value && !Internal::isButtonControl(binding.combo.primaryControl))
        {
            return ActionResult::InvalidControl;
        }

        if (!Internal::isFinite(binding.valueMapping.scale) ||
            !Internal::isFinite(binding.valueMapping.threshold) ||
            binding.valueMapping.threshold < 0.0f)
        {
            return ActionResult::InvalidBinding;
        }

        for (Input::InputControl modifier : binding.combo.modifiers)
        {
            if (!Internal::isValidControl(modifier) || !Internal::isButtonControl(modifier))
            {
                return ActionResult::InvalidControl;
            }
        }

        if (Internal::containsControl(binding.combo.modifiers, binding.combo.primaryControl) ||
            Internal::hasDuplicateControls(binding.combo.modifiers))
        {
            return ActionResult::InvalidBinding;
        }

        if ((binding.gesture.trigger == ActionTrigger::Hold &&
             (!Internal::isFinite(binding.gesture.holdSeconds) || binding.gesture.holdSeconds <= 0.0f)) ||
            (binding.gesture.trigger == ActionTrigger::DoubleTap &&
             (!Internal::isFinite(binding.gesture.doubleTapSeconds) || binding.gesture.doubleTapSeconds <= 0.0f)))
        {
            return ActionResult::InvalidBinding;
        }

        ActionResult result = ActionResult::Success;
        for (const ActionBinding<ActionEnum> &existingBinding : bindings)
        {
            if (ignoreSameActionBindings && existingBinding.action == binding.action)
            {
                continue;
            }

            if (Internal::areSameBinding(existingBinding, binding))
            {
                return ActionResult::DuplicateBinding;
            }

            if (Internal::hasBindingConflict(existingBinding, binding))
            {
                result = ActionResult::ConflictingBinding;
            }
        }

        return result;
    }

    template <typename ActionEnum>
    RebindResult ActionMap<ActionEnum>::buildRebindCapture(
        ActionEnum action,
        const Input::InputActivation &activation,
        Input::InputControl primaryControl,
        std::span<const Input::InputControl> modifierCandidates,
        const ActionRebindOptions &options,
        ActionRebindCapture<ActionEnum> &outCapture,
        std::span<const Input::InputControl> cancelControls,
        std::span<const Input::InputControl> ignoredControls) const
    {
        outCapture = {};
        outCapture.action = action;
        outCapture.replaceExistingBindings = options.replaceExistingBindings;

        if (!isValidAction(action))
        {
            outCapture.result = RebindResult::InvalidAction;
            return outCapture.result;
        }

        if (!Internal::isValidControl(primaryControl))
        {
            outCapture.result = RebindResult::InvalidControl;
            return outCapture.result;
        }

        ActionBinding<ActionEnum> binding{};
        binding.action = action;
        binding.combo.primaryControl = primaryControl;
        binding.combo.activationMode = options.activationMode;
        binding.gesture.trigger = options.trigger;
        binding.gesture.holdSeconds = options.holdSeconds;
        binding.gesture.doubleTapSeconds = options.doubleTapSeconds;
        binding.valueMapping.component = options.component;
        binding.valueMapping.scale = options.scale;
        binding.valueMapping.threshold = options.threshold;

        for (Input::InputControl modifier : modifierCandidates)
        {
            if (Internal::shouldCaptureModifier(
                    modifier,
                    binding.combo.primaryControl,
                    options.modifierMode,
                    cancelControls,
                    ignoredControls))
            {
                Internal::addUniqueControl(binding.combo.modifiers, modifier);
            }
        }

        const ActionResult validationResult = validateBinding(binding, options.replaceExistingBindings);
        if (validationResult == ActionResult::InvalidAction)
        {
            outCapture.result = RebindResult::InvalidAction;
            return outCapture.result;
        }

        if (validationResult == ActionResult::InvalidControl)
        {
            outCapture.result = RebindResult::InvalidControl;
            return outCapture.result;
        }

        if (validationResult == ActionResult::InvalidBinding || validationResult == ActionResult::InvalidSettings)
        {
            outCapture.result = RebindResult::InvalidBinding;
            return outCapture.result;
        }

        outCapture.binding = binding;
        outCapture.activation = activation;
        outCapture.result = RebindResult::Captured;
        return outCapture.result;
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::addBindingUnchecked(const ActionBinding<ActionEnum> &binding)
    {
        bindings.push_back(binding);
        bindingStates.push_back(RuntimeBindingState{});
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::applyBindingDown(ActionEnum action, bool down)
    {
        if (!isValidAction(action))
        {
            return;
        }

        ActionState &actionState = actionStates[getActionIndex(action)];
        actionState.down = actionState.down || down;
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::addBindingValue(ActionEnum action, const ActionValueMapping &valueMapping, float value)
    {
        if (!isValidAction(action))
        {
            return;
        }

        ActionValue &actionValue = actionStates[getActionIndex(action)].value;
        const float scaledValue = value * valueMapping.scale;

        switch (valueMapping.component)
        {
        case ActionComponent::Scalar:
            actionValue.scalar += scaledValue;
            break;
        case ActionComponent::X:
            actionValue.x += scaledValue;
            break;
        case ActionComponent::Y:
            actionValue.y += scaledValue;
            break;
        }
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::processActionValues()
    {
        for (std::size_t actionIndex = 0; actionIndex < actionStates.size(); ++actionIndex)
        {
            ActionState &actionState = actionStates[actionIndex];
            const ActionSettings &settings = actionSettings[actionIndex];

            if (settings.kind == ActionKind::Button)
            {
                continue;
            }

            if (settings.kind == ActionKind::Axis1D)
            {
                float value = actionState.value.scalar;

                if (settings.deadzoneMode != DeadzoneMode::None)
                {
                    value = Internal::applyDeadzone1D(value, settings.innerDeadzone, settings.outerDeadzone);
                }

                value = Internal::applyCurve1D(value, settings.curveExponent);
                value *= settings.sensitivity;

                if (settings.invert)
                {
                    value = -value;
                }

                if (settings.clampValue)
                {
                    value = Internal::clamp(value, -1.0f, 1.0f);
                }

                actionState.value.scalar = value;
                actionState.value.x = 0.0f;
                actionState.value.y = 0.0f;

                const bool isActive = Internal::absoluteValue(value) > settings.activationThreshold;
                actionState.down = actionState.down || isActive;
                actionState.pressed = actionState.pressed || (isActive && !actionState.valueActive);
                actionState.released = actionState.released || (!isActive && actionState.valueActive);
                actionState.valueActive = isActive;
                continue;
            }

            float x = actionState.value.x;
            float y = actionState.value.y;

            switch (settings.deadzoneMode)
            {
            case DeadzoneMode::Axial:
                Internal::applyAxialDeadzone2D(x, y, settings.innerDeadzone, settings.outerDeadzone);
                break;
            case DeadzoneMode::Radial:
                Internal::applyRadialDeadzone2D(x, y, settings.innerDeadzone, settings.outerDeadzone);
                break;
            case DeadzoneMode::None:
                break;
            }

            Internal::applyCurve2D(x, y, settings.curveExponent);
            x *= settings.sensitivity;
            y *= settings.sensitivity;

            if (settings.invertX)
            {
                x = -x;
            }

            if (settings.invertY)
            {
                y = -y;
            }

            if (settings.normalizeDiagonal)
            {
                Internal::clampVectorLength(x, y, 1.0f);
            }

            if (settings.clampValue)
            {
                Internal::clampVectorLength(x, y, 1.0f);
            }

            actionState.value.scalar = 0.0f;
            actionState.value.x = x;
            actionState.value.y = y;

            const bool isActive = Internal::getVectorLength(x, y) > settings.activationThreshold;
            actionState.down = actionState.down || isActive;
            actionState.pressed = actionState.pressed || (isActive && !actionState.valueActive);
            actionState.released = actionState.released || (!isActive && actionState.valueActive);
            actionState.valueActive = isActive;
        }
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::hasMatchingHoldFired(std::size_t bindingIndex) const
    {
        if (bindingIndex >= bindings.size())
        {
            return false;
        }

        const ActionBinding<ActionEnum> &binding = bindings[bindingIndex];
        for (std::size_t compareIndex = 0; compareIndex < bindings.size(); ++compareIndex)
        {
            if (compareIndex == bindingIndex)
            {
                continue;
            }

            if (bindings[compareIndex].gesture.trigger == ActionTrigger::Hold &&
                Internal::areSameCombo(bindings[compareIndex].combo, binding.combo) &&
                bindingStates[compareIndex].holdFired)
            {
                return true;
            }
        }

        return false;
    }
}
