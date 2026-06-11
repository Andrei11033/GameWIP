#pragma once

namespace GameWIP::Action
{
    template <typename ActionEnum>
    ActionBindingBuilder<ActionEnum>::ActionBindingBuilder(ActionMap<ActionEnum> &actionMap, ActionEnum action)
        : actionMap(actionMap)
    {
        binding.action = action;
    }

    template <typename ActionEnum> ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::on(Input::InputControl primaryControl)
    {
        binding.combo.primaryControl = primaryControl;
        return *this;
    }

    template <typename ActionEnum> ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::withModifier(Input::InputControl modifier)
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

    template <typename ActionEnum> ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::primaryLast()
    {
        binding.combo.activationMode = ComboActivationMode::PrimaryLast;
        return *this;
    }

    template <typename ActionEnum> ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::anyOrder()
    {
        binding.combo.activationMode = ComboActivationMode::AnyOrder;
        return *this;
    }

    template <typename ActionEnum> ActionBindingBuilder<ActionEnum> &ActionBindingBuilder<ActionEnum>::withSettings(const ActionSettings &settings)
    {
        binding.settings = settings;
        binding.hasCustomSettings = true;
        return *this;
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::pressed()
    {
        return add(ActionTrigger::Pressed, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::released()
    {
        return add(ActionTrigger::Released, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::down()
    {
        return add(ActionTrigger::Down, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::tap()
    {
        return add(ActionTrigger::Tap, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, 0.0f);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::hold(float seconds)
    {
        return add(ActionTrigger::Hold, ActionComponent::Scalar, 1.0f, 0.0f, seconds, 0.0f);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::doubleTap(float seconds)
    {
        return add(ActionTrigger::DoubleTap, ActionComponent::Scalar, 1.0f, 0.0f, 0.0f, seconds);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::value(ActionComponent component, float scale, float threshold)
    {
        return add(ActionTrigger::Value, component, scale, threshold, 0.0f, 0.0f);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::axis1D(float scale, float threshold)
    {
        return value(ActionComponent::Scalar, scale, threshold);
    }

    template <typename ActionEnum> ActionResult ActionBindingBuilder<ActionEnum>::axis2D(ActionComponent component, float scale, float threshold)
    {
        if (component == ActionComponent::Scalar)
        {
            return ActionResult::InvalidBinding;
        }

        return value(component, scale, threshold);
    }

    template <typename ActionEnum>
    ActionResult ActionBindingBuilder<ActionEnum>::add(
        ActionTrigger trigger,
        ActionComponent component,
        float scale,
        float threshold,
        float holdSeconds,
        float doubleTapSeconds)
    {
        binding.gesture.trigger = trigger;
        binding.gesture.holdSeconds = holdSeconds;
        binding.gesture.doubleTapSeconds = doubleTapSeconds;
        binding.valueMapping.component = component;
        binding.valueMapping.scale = scale;
        binding.valueMapping.threshold = threshold;

        return actionMap.addBinding(binding);
    }
} // namespace GameWIP::Action
