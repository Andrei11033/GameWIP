#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "input/input.h"

namespace GameWIP::Action
{
    enum class ActionKind
    {
        Button,
        Axis1D,
        Axis2D
    };

    enum class ActionTrigger
    {
        Pressed,
        Released,
        Down,
        Hold,
        Tap,
        DoubleTap,
        Value
    };

    enum class ActionComponent
    {
        Scalar,
        X,
        Y
    };

    enum class ComboActivationMode
    {
        PrimaryLast,
        AnyOrder
    };

    enum class DeadzoneMode
    {
        None,
        Axial,
        Radial
    };

    enum class ActionResult
    {
        Success,
        InvalidAction,
        InvalidControl,
        InvalidBinding,
        InvalidSettings,
        DuplicateBinding,
        ConflictingBinding
    };

    /// @brief Controls which held buttons are captured as rebinding modifiers.
    enum class RebindModifierMode
    {
        None,                  // Do not capture modifiers.
        KeyboardModifiersOnly, // Capture held Ctrl/Shift/Alt/Super keys.
        AllHeldButtons         // Capture all held buttons except the primary control.
    };

    /// @brief Controls when a rebind capture is completed.
    enum class RebindCompletionMode
    {
        OnActivation, // Finish as soon as a valid control activates.
        OnRelease     // Collect pressed buttons and finish when one is released.
    };

    /// @brief Result for a rebinding capture attempt.
    enum class RebindResult
    {
        None,           // No usable activation was found.
        Collecting,     // Capture is active and waiting for completion.
        Captured,       // A binding was captured.
        Canceled,       // A cancel control was activated.
        InvalidAction,  // Target action is outside the map.
        InvalidControl, // Captured control is not bindable.
        InvalidBinding  // Rebind options produced an invalid binding.
    };

    struct ActionSettings
    {
        ActionKind kind = ActionKind::Button;           // Type of action (button/axis1d/axis2d).
        bool clampValue = true;                         // Clamp final value to valid range.
        bool normalizeDiagonal = false;                 // Normalize 2D diagonal input to unit length.
        DeadzoneMode deadzoneMode = DeadzoneMode::None; // Deadzone processing mode.
        float innerDeadzone = 0.0f;                     // Inner deadzone threshold.
        float outerDeadzone = 1.0f;                     // Outer deadzone threshold.
        float sensitivity = 1.0f;                       // Value multiplier after processing.
        float curveExponent = 1.0f;                     // Curve exponent for non-linear response.
        bool invert = false;                            // Invert scalar output.
        bool invertX = false;                           // Invert X axis output.
        bool invertY = false;                           // Invert Y axis output.
        float activationThreshold = 0.5f;               // Threshold for pressed/down states.
    };

    /// @brief Creates default button action settings.
    /// @return Button settings.
    ActionSettings makeButtonSettings();

    /// @brief Creates one-dimensional axis settings.
    /// @param deadzoneMode Deadzone behavior.
    /// @param innerDeadzone Value ignored near the center.
    /// @param outerDeadzone Value treated as full strength.
    /// @param sensitivity Multiplier applied after deadzone/curve.
    /// @param invert True to flip the scalar value.
    /// @param clampValue True to clamp final value to -1..1.
    /// @param activationThreshold Value needed for pressed/down state.
    /// @return Axis1D settings.
    ActionSettings makeAxis1DSettings(
        DeadzoneMode deadzoneMode = DeadzoneMode::None,
        float innerDeadzone = 0.0f,
        float outerDeadzone = 1.0f,
        float sensitivity = 1.0f,
        bool invert = false,
        bool clampValue = true,
        float activationThreshold = 0.5f);

    /// @brief Creates two-dimensional axis settings.
    /// @param deadzoneMode Deadzone behavior.
    /// @param innerDeadzone Value ignored near the center.
    /// @param outerDeadzone Value treated as full strength.
    /// @param sensitivity Multiplier applied after deadzone/curve.
    /// @param invertX True to flip x.
    /// @param invertY True to flip y.
    /// @param clampValue True to clamp final vector length to 1.
    /// @param normalizeDiagonal True to clamp diagonal button movement to length 1.
    /// @param activationThreshold Vector length needed for pressed/down state.
    /// @return Axis2D settings.
    ActionSettings makeAxis2DSettings(
        DeadzoneMode deadzoneMode = DeadzoneMode::None,
        float innerDeadzone = 0.0f,
        float outerDeadzone = 1.0f,
        float sensitivity = 1.0f,
        bool invertX = false,
        bool invertY = false,
        bool clampValue = true,
        bool normalizeDiagonal = false,
        float activationThreshold = 0.5f);

    /// @brief Creates digital movement settings.
    /// @return Axis2D settings for keyboard/controller movement.
    ActionSettings makeMovementSettings();

    /// @brief Creates raw mouse-look settings.
    /// @param sensitivity Mouse delta multiplier.
    /// @param invertX True to flip x.
    /// @param invertY True to flip y.
    /// @return Axis2D settings for raw mouse movement.
    ActionSettings makeMouseLookSettings(float sensitivity = 1.0f, bool invertX = false, bool invertY = false);

    /// @brief Creates gamepad stick settings.
    /// @param innerDeadzone Value ignored near the center.
    /// @param outerDeadzone Value treated as full strength.
    /// @param sensitivity Stick value multiplier.
    /// @param invertX True to flip x.
    /// @param invertY True to flip y.
    /// @return Axis2D settings for sticks.
    ActionSettings makeGamepadStickSettings(
        float innerDeadzone = 0.15f,
        float outerDeadzone = 1.0f,
        float sensitivity = 1.0f,
        bool invertX = false,
        bool invertY = false);

    /// @brief Creates gamepad trigger settings.
    /// @param innerDeadzone Value ignored near the trigger rest point.
    /// @param outerDeadzone Value treated as full strength.
    /// @param sensitivity Trigger value multiplier.
    /// @param invert True to flip the scalar value.
    /// @return Axis1D settings for triggers.
    ActionSettings makeGamepadTriggerSettings(
        float innerDeadzone = 0.05f,
        float outerDeadzone = 1.0f,
        float sensitivity = 1.0f,
        bool invert = false);

    struct ActionCombo
    {
        Input::InputControl primaryControl{};                                  // Primary control that triggers the binding.
        std::vector<Input::InputControl> modifiers{};                          // Modifiers that must be held when activating.
        ComboActivationMode activationMode = ComboActivationMode::PrimaryLast; // When combo is activated.
    };

    struct ActionGesture
    {
        ActionTrigger trigger = ActionTrigger::Pressed; // Type of trigger event.
        float holdSeconds = 0.0f;                       // Required hold duration for Hold trigger.
        float doubleTapSeconds = 0.0f;                  // Window for second tap in DoubleTap trigger.
    };

    struct ActionValueMapping
    {
        ActionComponent component = ActionComponent::Scalar; // Which value component to use.
        float scale = 1.0f;                                  // Scale applied to the component value.
        float threshold = 0.0f;                              // Minimum activation threshold.
    };

    /// @brief Options used when capturing a new binding from player input.
    struct ActionRebindOptions
    {
        ActionTrigger trigger = ActionTrigger::Pressed;                              // Trigger assigned to the captured binding.
        ActionComponent component = ActionComponent::Scalar;                         // Value component for axis bindings.
        float scale = 1.0f;                                                          // Value scale for the captured binding.
        float threshold = 0.0f;                                                      // Value threshold for the captured binding.
        float holdSeconds = 0.0f;                                                    // Hold duration when trigger is Hold.
        float doubleTapSeconds = 0.0f;                                               // Tap window when trigger is DoubleTap.
        ComboActivationMode activationMode = ComboActivationMode::PrimaryLast;       // Combo ordering behavior.
        RebindModifierMode modifierMode = RebindModifierMode::KeyboardModifiersOnly; // Modifier capture policy.
        RebindCompletionMode completionMode = RebindCompletionMode::OnActivation;    // Capture completion behavior.
        ActionSettings settings{};                                                   // Optional settings assigned to the captured binding.
        bool hasCustomSettings = false;                                               // True when settings should be copied to the captured binding.
        bool replaceExistingBindings = true;                                         // Replace existing bindings for the same action when applied.
    };

    template <typename ActionEnum>
    struct ActionBinding
    {
        ActionEnum action{};               // The action being bound.
        ActionCombo combo{};               // Combo control configuration.
        ActionGesture gesture{};           // Gesture/trigger configuration.
        ActionValueMapping valueMapping{}; // Value component and scale configuration.
        ActionSettings settings{};         // Optional per-binding value processing settings.
        bool hasCustomSettings = false;    // True when settings overrides the action-kind default.
    };

    /// @brief Captured binding waiting to be applied.
    template <typename ActionEnum>
    struct ActionRebindCapture
    {
        ActionEnum action{};                      // Target action.
        ActionBinding<ActionEnum> binding{};      // Captured binding.
        Input::InputActivation activation{};      // Activation that produced the binding.
        RebindResult result = RebindResult::None; // Capture result.
        bool replaceExistingBindings = true;      // Whether apply should replace existing bindings for the action.
    };

    /// @brief Stateful rebind capture that can collect multi-key combos over multiple frames.
    template <typename ActionEnum>
    struct ActionRebindSession
    {
        ActionEnum action{};                         // Target action.
        ActionRebindOptions options{};               // Capture behavior.
        std::vector<Input::InputControl> controls{}; // Controls collected during capture.
        Input::InputControl primaryControl{};        // Control used as the binding primary.
        Input::InputActivation activation{};         // Activation that selected the primary control.
        bool active = false;                         // True while capture is in progress.
        bool hasPrimaryControl = false;              // True after at least one valid control was collected.
    };

    struct ActionValue
    {
        float scalar = 0.0f; // Scalar value (used for 1D axes).
        float x = 0.0f;      // X value (used for 2D axes).
        float y = 0.0f;      // Y value (used for 2D axes).
    };

    struct ActionState
    {
        bool down = false;        // True while the action is held down.
        bool pressed = false;     // True when the action transitioned to down this frame.
        bool released = false;    // True when the action transitioned to up this frame.
        bool valueActive = false; // True when the value is above the activation threshold.
        ActionValue value{};      // Current action value.
    };

    struct RuntimeBindingState
    {
        bool active = false;              // True while the binding is active.
        bool holdFired = false;           // True once hold duration is satisfied.
        float heldSeconds = 0.0f;         // Time held in seconds.
        float timeSinceLastTap = 0.0f;    // Time since last tap for DoubleTap detection.
        bool waitingForSecondTap = false; // True while collecting second tap.
    };

    template <typename ActionEnum>
    class ActionMap;

    template <typename ActionEnum>
    class ActionBindingBuilder
    {
    public:
        ActionBindingBuilder(ActionMap<ActionEnum> &actionMap, ActionEnum action);

        ActionBindingBuilder &on(Input::InputControl primaryControl);
        ActionBindingBuilder &withModifier(Input::InputControl modifier);
        ActionBindingBuilder &withModifiers(std::span<const Input::InputControl> modifiers);
        ActionBindingBuilder &primaryLast();
        ActionBindingBuilder &anyOrder();
        ActionBindingBuilder &withSettings(const ActionSettings &settings);

        ActionResult pressed();
        ActionResult released();
        ActionResult down();
        ActionResult tap();
        ActionResult hold(float seconds);
        ActionResult doubleTap(float seconds);
        ActionResult value(ActionComponent component = ActionComponent::Scalar, float scale = 1.0f, float threshold = 0.0f);
        ActionResult axis1D(float scale = 1.0f, float threshold = 0.0f);
        ActionResult axis2D(ActionComponent component, float scale = 1.0f, float threshold = 0.0f);

    private:
        ActionMap<ActionEnum> &actionMap;
        ActionBinding<ActionEnum> binding{};

        ActionResult add(ActionTrigger trigger, ActionComponent component, float scale, float threshold, float holdSeconds, float doubleTapSeconds);
    };

    template <typename ActionEnum>
    class ActionMap
    {
    public:
        explicit ActionMap(ActionEnum actionCount);

        void resize(ActionEnum actionCount);
        std::size_t getActionCount() const;

        bool isValidAction(ActionEnum action) const;

        ActionResult defineAction(ActionEnum action, ActionKind kind);

        void advanceFrame();
        void evaluate(const Input::InputState &inputState, float deltaSeconds);

        /// @brief Returns whether captured text input is available.
        /// @return True if text was received during the last evaluation.
        bool hasTextInput() const;

        /// @brief Returns captured UTF-8 text input.
        /// @return Text received during the last evaluation.
        std::string_view getTextInputUtf8() const;

        /// @brief Returns whether captured mouse position is valid.
        /// @return True if the mouse position is known.
        bool hasMousePosition() const;

        /// @brief Returns captured mouse X position.
        /// @return Latest client-area x position.
        int getMouseX() const;

        /// @brief Returns captured mouse Y position.
        /// @return Latest client-area y position.
        int getMouseY() const;

        /// @brief Returns captured raw mouse X movement.
        /// @return Raw x movement from the last evaluation.
        int getMouseDeltaX() const;

        /// @brief Returns captured raw mouse Y movement.
        /// @return Raw y movement from the last evaluation.
        int getMouseDeltaY() const;

        /// @brief Returns captured mouse wheel movement.
        /// @param wheel Wheel axis to query.
        /// @return Wheel movement from the last evaluation.
        float getMouseWheelDelta(Input::MouseWheel wheel) const;

        bool isDown(ActionEnum action) const;
        bool wasPressed(ActionEnum action) const;
        bool wasReleased(ActionEnum action) const;

        float getValue(ActionEnum action) const;
        float getValueX(ActionEnum action) const;
        float getValueY(ActionEnum action) const;

        std::span<const ActionBinding<ActionEnum>> getBindings() const;

        ActionBindingBuilder<ActionEnum> bind(ActionEnum action);
        ActionResult addBinding(const ActionBinding<ActionEnum> &binding);

        /// @brief Starts a stateful rebind capture.
        /// @param action Action to bind.
        /// @param options Rebinding behavior.
        /// @param outSession Session that receives capture state.
        /// @return Collecting on success, or an error result.
        RebindResult beginBindingCapture(
            ActionEnum action,
            const ActionRebindOptions &options,
            ActionRebindSession<ActionEnum> &outSession) const;

        /// @brief Updates a stateful rebind capture.
        /// @param inputState Raw input state to inspect.
        /// @param session Active capture session.
        /// @param outCapture Captured binding data.
        /// @param cancelControls Controls that cancel capture.
        /// @param ignoredControls Controls ignored during capture.
        /// @return Capture result.
        RebindResult updateBindingCapture(
            const Input::InputState &inputState,
            ActionRebindSession<ActionEnum> &session,
            ActionRebindCapture<ActionEnum> &outCapture,
            std::span<const Input::InputControl> cancelControls = {},
            std::span<const Input::InputControl> ignoredControls = {}) const;

        /// @brief Captures a binding from the newest valid input activation.
        /// @param action Action to bind.
        /// @param inputState Raw input state to inspect.
        /// @param options Rebinding behavior.
        /// @param outCapture Captured binding data.
        /// @param cancelControls Controls that cancel capture.
        /// @param ignoredControls Controls ignored during capture.
        /// @return Capture result.
        RebindResult captureBinding(
            ActionEnum action,
            const Input::InputState &inputState,
            const ActionRebindOptions &options,
            ActionRebindCapture<ActionEnum> &outCapture,
            std::span<const Input::InputControl> cancelControls = {},
            std::span<const Input::InputControl> ignoredControls = {}) const;

        /// @brief Applies a previously captured binding.
        /// @param capture Captured binding to apply.
        /// @return Result from binding validation/add.
        ActionResult applyCapturedBinding(const ActionRebindCapture<ActionEnum> &capture);
        void clearBindings(ActionEnum action);
        void clearAllBindings();

    private:
        std::vector<ActionKind> actionKinds{};             // Kind for each action.
        std::vector<ActionState> actionStates{};           // Current state for each action.
        std::vector<ActionBinding<ActionEnum>> bindings{}; // All active input bindings.
        std::vector<RuntimeBindingState> bindingStates{};  // Runtime state for each binding.
        std::string textInputUtf8Snapshot{};               // Captured UTF-8 text input.
        int mouseDeltaXSnapshot = 0;                       // Captured raw mouse X movement.
        int mouseDeltaYSnapshot = 0;                       // Captured raw mouse Y movement.
        int mouseXSnapshot = 0;                            // Captured mouse X position.
        int mouseYSnapshot = 0;                            // Captured mouse Y position.
        float verticalWheelDeltaSnapshot = 0.0f;           // Captured vertical wheel delta.
        float horizontalWheelDeltaSnapshot = 0.0f;         // Captured horizontal wheel delta.
        bool mousePositionKnownSnapshot = false;           // Whether mouse position is valid.

        std::size_t getActionIndex(ActionEnum action) const;
        void copyInputSnapshot(const Input::InputState &inputState);
        ActionResult validateBinding(const ActionBinding<ActionEnum> &binding, bool ignoreSameActionBindings) const;
        RebindResult buildRebindCapture(
            ActionEnum action,
            const Input::InputActivation &activation,
            Input::InputControl primaryControl,
            std::span<const Input::InputControl> modifierCandidates,
            const ActionRebindOptions &options,
            ActionRebindCapture<ActionEnum> &outCapture,
            std::span<const Input::InputControl> cancelControls,
            std::span<const Input::InputControl> ignoredControls) const;
        void addBindingUnchecked(const ActionBinding<ActionEnum> &binding);
        void applyBindingDown(ActionEnum action, bool down);
        bool hasMatchingHoldFired(std::size_t bindingIndex) const;
    };

}

#include "action/internal/action.inl"
