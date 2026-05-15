#pragma once

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
            case Input::InputActivationType::AxisChanged:
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

        inline bool isAxisActivation(const Input::InputActivation &activation)
        {
            return activation.control.controlType == Input::InputControlType::Axis &&
                   (activation.activationType == Input::InputActivationType::AxisPositive ||
                    activation.activationType == Input::InputActivationType::AxisNegative ||
                    activation.activationType == Input::InputActivationType::AxisChanged);
        }

        inline bool isAllowedCaptureDevice(const Input::InputActivation &activation, const ActionRebindOptions &options)
        {
            return !options.hasDeviceFilter ||
                   (activation.control.deviceType == options.deviceFilter.deviceType &&
                    activation.control.deviceIndex == options.deviceFilter.deviceIndex);
        }

        inline bool passesAxisCaptureThresholds(const Input::InputActivation &activation, const ActionRebindOptions &options)
        {
            if (!isAxisActivation(activation))
            {
                return true;
            }

            const float valueMagnitude = activation.value < 0.0f ? -activation.value : activation.value;
            const float movement = activation.value - activation.previousValue;
            const float movementMagnitude = movement < 0.0f ? -movement : movement;
            return valueMagnitude >= options.axisActivationThreshold &&
                   movementMagnitude >= options.axisNoiseThreshold;
        }

        inline bool isAllowedCaptureActivation(const Input::InputActivation &activation, const ActionRebindOptions &options)
        {
            return isCaptureActivation(activation) &&
                   isAllowedCaptureDevice(activation, options) &&
                   passesAxisCaptureThresholds(activation, options);
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

        inline bool areSameActionSettings(const ActionSettings &left, const ActionSettings &right)
        {
            return left.kind == right.kind &&
                   left.clampValue == right.clampValue &&
                   left.normalizeDiagonal == right.normalizeDiagonal &&
                   left.deadzoneMode == right.deadzoneMode &&
                   left.innerDeadzone == right.innerDeadzone &&
                   left.outerDeadzone == right.outerDeadzone &&
                   left.sensitivity == right.sensitivity &&
                   left.curveExponent == right.curveExponent &&
                   left.invert == right.invert &&
                   left.invertX == right.invertX &&
                   left.invertY == right.invertY &&
                   left.activationThreshold == right.activationThreshold;
        }

        inline ActionSettings makeDefaultSettings(ActionKind kind)
        {
            switch (kind)
            {
            case ActionKind::Button:
                return makeButtonSettings();
            case ActionKind::Axis1D:
                return makeAxis1DSettings();
            case ActionKind::Axis2D:
                return makeAxis2DSettings();
            }

            return makeButtonSettings();
        }

        template <typename ActionEnum>
        ActionSettings getBindingSettings(const ActionBinding<ActionEnum> &binding, ActionKind kind)
        {
            return binding.hasCustomSettings ? binding.settings : makeDefaultSettings(kind);
        }

        template <typename ActionEnum>
        struct ValueBucket
        {
            ActionEnum action{};
            Input::InputDeviceType deviceType = Input::InputDeviceType::Keyboard;
            Input::DeviceIndex deviceIndex = 0;
            ActionSettings settings{};
            ActionValue value{};
            std::uint64_t lastChangeSequence = 0;
        };

        template <typename ActionEnum>
        bool isSameValueBucket(const ValueBucket<ActionEnum> &bucket, ActionEnum action, Input::InputControl control, const ActionSettings &settings)
        {
            return bucket.action == action &&
                   bucket.deviceType == control.deviceType &&
                   bucket.deviceIndex == control.deviceIndex &&
                   areSameActionSettings(bucket.settings, settings);
        }

        template <typename ActionEnum>
        void addValueToBucket(
            std::vector<ValueBucket<ActionEnum>> &buckets,
            const ActionBinding<ActionEnum> &binding,
            ActionKind kind,
            float value,
            std::uint64_t lastChangeSequence)
        {
            const ActionSettings settings = getBindingSettings(binding, kind);
            auto bucket = std::find_if(
                buckets.begin(),
                buckets.end(),
                [&](const ValueBucket<ActionEnum> &candidate)
                {
                    return isSameValueBucket(candidate, binding.action, binding.combo.primaryControl, settings);
                });

            if (bucket == buckets.end())
            {
                buckets.push_back(ValueBucket<ActionEnum>{
                    .action = binding.action,
                    .deviceType = binding.combo.primaryControl.deviceType,
                    .deviceIndex = binding.combo.primaryControl.deviceIndex,
                    .settings = settings});
                bucket = buckets.end() - 1;
            }

            if (lastChangeSequence > bucket->lastChangeSequence)
            {
                bucket->lastChangeSequence = lastChangeSequence;
            }

            const float scaledValue = value * binding.valueMapping.scale;
            switch (binding.valueMapping.component)
            {
            case ActionComponent::Scalar:
                bucket->value.scalar += scaledValue;
                break;
            case ActionComponent::X:
                bucket->value.x += scaledValue;
                break;
            case ActionComponent::Y:
                bucket->value.y += scaledValue;
                break;
            }
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

        inline bool shouldChooseValueCandidate(
            float currentMagnitude,
            std::uint64_t currentSequence,
            float candidateMagnitude,
            std::uint64_t candidateSequence)
        {
            if (candidateMagnitude > currentMagnitude)
            {
                return true;
            }

            if (candidateMagnitude < currentMagnitude)
            {
                return false;
            }

            return candidateSequence > currentSequence;
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
}
