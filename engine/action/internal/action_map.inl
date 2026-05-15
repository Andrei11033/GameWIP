#pragma once

namespace GameWIP::Action
{
    template <typename ActionEnum>
    ActionMap<ActionEnum>::ActionMap(ActionEnum actionCount)
    {
        resize(actionCount);
    }

    template <typename ActionEnum>
    void ActionMap<ActionEnum>::resize(ActionEnum actionCount)
    {
        const std::size_t count = static_cast<std::size_t>(actionCount);
        actionKinds.resize(count);
        actionStates.resize(count);
    }

    template <typename ActionEnum>
    std::size_t ActionMap<ActionEnum>::getActionCount() const
    {
        return actionKinds.size();
    }

    template <typename ActionEnum>
    bool ActionMap<ActionEnum>::isValidAction(ActionEnum action) const
    {
        return getActionIndex(action) < actionKinds.size();
    }

    template <typename ActionEnum>
    ActionResult ActionMap<ActionEnum>::defineAction(ActionEnum action, ActionKind kind)
    {
        if (!isValidAction(action))
        {
            return ActionResult::InvalidAction;
        }

        actionKinds[getActionIndex(action)] = kind;
        return ActionResult::Success;
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
        std::vector<Internal::ValueBucket<ActionEnum>> valueBuckets;

        for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
        {
            const ActionBinding<ActionEnum> &binding = bindings[bindingIndex];
            RuntimeBindingState &bindingState = bindingStates[bindingIndex];

            if (!isValidAction(binding.action))
            {
                continue;
            }

            const ActionKind actionKind = actionKinds[getActionIndex(binding.action)];
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
            }

            if (bindingContributesValue)
            {
                Internal::addValueToBucket(valueBuckets, binding, actionKind, bindingValue);
            }

            bindingState.active = nextRuntimeActive;
        }

        std::vector<bool> valueActive(actionStates.size(), false);
        for (const Internal::ValueBucket<ActionEnum> &bucket : valueBuckets)
        {
            if (!isValidAction(bucket.action))
            {
                continue;
            }

            ActionState &actionState = actionStates[getActionIndex(bucket.action)];
            const ActionSettings &settings = bucket.settings;

            if (settings.kind == ActionKind::Axis1D)
            {
                float value = bucket.value.scalar;

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

                actionState.value.scalar += value;
                if (Internal::absoluteValue(value) > settings.activationThreshold)
                {
                    valueActive[getActionIndex(bucket.action)] = true;
                }
                continue;
            }

            if (settings.kind == ActionKind::Axis2D)
            {
                float x = bucket.value.x;
                float y = bucket.value.y;

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

                actionState.value.x += x;
                actionState.value.y += y;
                if (Internal::getVectorLength(x, y) > settings.activationThreshold)
                {
                    valueActive[getActionIndex(bucket.action)] = true;
                }
            }
        }

        for (std::size_t actionIndex = 0; actionIndex < actionStates.size(); ++actionIndex)
        {
            if (actionKinds[actionIndex] == ActionKind::Button)
            {
                continue;
            }

            ActionState &actionState = actionStates[actionIndex];
            const bool isActive = valueActive[actionIndex];
            actionState.down = actionState.down || isActive;
            actionState.pressed = actionState.pressed || (isActive && !actionState.valueActive);
            actionState.released = actionState.released || (!isActive && actionState.valueActive);
            actionState.valueActive = isActive;
        }
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

        const ActionKind actionKind = actionKinds[getActionIndex(binding.action)];
        if (!Internal::isBindingCompatibleWithActionKind(actionKind, binding))
        {
            return ActionResult::InvalidBinding;
        }

        if (binding.hasCustomSettings &&
            (!Internal::isValidActionSettings(binding.settings) || binding.settings.kind != actionKind))
        {
            return ActionResult::InvalidSettings;
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
        binding.settings = options.settings;
        binding.hasCustomSettings = options.hasCustomSettings;

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
