/// @file action.h
/// @brief Source-tree action mapping, binding, and frame-evaluation interface.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

    /// @brief Controls how analog axes are converted into captured bindings.
    enum class AxisCaptureMode
    {
        FullAxis,       // Capture the whole axis and keep the requested scale.
        DirectionalAxis // Capture the moved direction by applying the movement sign to the requested scale.
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
        ActionKind kind = ActionKind::Button;
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

    /// @brief Creates default button-action settings.
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
    ActionSettings makeGamepadTriggerSettings(float innerDeadzone = 0.05f, float outerDeadzone = 1.0f, float sensitivity = 1.0f, bool invert = false);

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
        AxisCaptureMode axisCaptureMode = AxisCaptureMode::FullAxis;                 // Axis capture behavior.
        Input::InputDeviceRef deviceFilter{};                                        // Device allowed to complete capture.
        bool hasDeviceFilter = false;                                                // True to capture from only deviceFilter.
        float axisActivationThreshold = 0.35f;                                       // Minimum absolute axis value for capture.
        float axisNoiseThreshold = 0.08f;                                            // Minimum axis movement for capture.
        ActionSettings settings{};                                                   // Optional settings assigned to the captured binding.
        bool hasCustomSettings = false;                                              // True when settings should be copied to the captured binding.
        bool replaceExistingBindings = true;                                         // Replace existing bindings for the same action when applied.
    };

    template <typename ActionEnum> struct ActionBinding
    {
        ActionEnum action{};               // The action being bound.
        ActionCombo combo{};               // Combo control configuration.
        ActionGesture gesture{};           // Gesture/trigger configuration.
        ActionValueMapping valueMapping{}; // Value component and scale configuration.
        ActionSettings settings{};         // Optional per-binding value processing settings.
        bool hasCustomSettings = false;    // True when settings overrides the action-kind default.
    };

    /// @brief Captured binding waiting to be applied.
    template <typename ActionEnum> struct ActionRebindCapture
    {
        ActionEnum action{};                      // Target action.
        ActionBinding<ActionEnum> binding{};      // Captured binding.
        Input::InputActivation activation{};      // Activation that produced the binding.
        RebindResult result = RebindResult::None; // Capture result.
        bool replaceExistingBindings = true;      // Whether apply should replace existing bindings for the action.
    };

    /// @brief Stateful rebind capture that can collect multi-key combos over multiple frames.
    template <typename ActionEnum> struct ActionRebindSession
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
        bool active = false;                   // True while the binding is active.
        bool holdFired = false;                // True once hold duration is satisfied.
        float heldSeconds = 0.0f;              // Time held in seconds.
        float timeSinceLastTap = 0.0f;         // Time since last tap for DoubleTap detection.
        std::uint64_t valueChangeSequence = 0; // Last change order for value candidate tie-breaks.
        bool waitingForSecondTap = false;      // True while collecting second tap.
    };

    template <typename ActionEnum> class ActionMap;

    /// @brief Builds one binding and stores it when a trigger such as pressed(), hold(), or value() is selected.
    /// @tparam ActionEnum Contiguous action enum whose values are used as ActionMap indices.
    template <typename ActionEnum> class ActionBindingBuilder
    {
    public:
        /// @brief Creates a builder that targets one action in the supplied map.
        /// @param map Map that receives the completed binding; the builder must not outlive it.
        /// @param action Action slot targeted by the completed binding.
        ActionBindingBuilder(ActionMap<ActionEnum> &map, ActionEnum action);

        /// @brief Replaces the primary control for the binding.
        ActionBindingBuilder &on(Input::InputControl primaryControl);
        /// @brief Appends one modifier to the pending combo.
        ActionBindingBuilder &withModifier(Input::InputControl modifier);
        /// @brief Appends all supplied modifiers in their existing order.
        ActionBindingBuilder &withModifiers(std::span<const Input::InputControl> modifiers);
        /// @brief Requires the primary control to activate after all modifiers.
        ActionBindingBuilder &primaryLast();
        /// @brief Allows the primary control and modifiers to activate in any order.
        ActionBindingBuilder &anyOrder();
        /// @brief Copies per-binding value-processing settings into the pending binding.
        ActionBindingBuilder &withSettings(const ActionSettings &settings);

        /// @brief Adds a pressed-trigger binding and returns its validation result.
        ActionResult pressed();
        /// @brief Adds a released-trigger binding and returns its validation result.
        ActionResult released();
        /// @brief Adds a held-down binding and returns its validation result.
        ActionResult down();
        /// @brief Adds a tap-trigger binding and returns its validation result.
        ActionResult tap();
        /// @brief Adds a hold-trigger binding; seconds must be positive and finite.
        ActionResult hold(float seconds);
        /// @brief Adds a double-tap binding; seconds must be positive and finite.
        ActionResult doubleTap(float seconds);
        /// @brief Adds a value binding using one action component and mapping.
        /// @param component Scalar for 1D actions, or X/Y for 2D actions.
        /// @param scale Multiplier applied to the selected input component.
        /// @param threshold Non-negative activation threshold for the input value.
        ActionResult value(ActionComponent component = ActionComponent::Scalar, float scale = 1.0f, float threshold = 0.0f);
        /// @brief Adds a scalar value binding for a 1D action.
        /// @param scale Multiplier applied to the input value.
        /// @param threshold Non-negative activation threshold for the input value.
        ActionResult axis1D(float scale = 1.0f, float threshold = 0.0f);
        /// @brief Adds an X or Y component binding for a 2D action.
        /// @param component Must be X or Y; Scalar is rejected.
        /// @param scale Multiplier applied to the selected input component.
        /// @param threshold Non-negative activation threshold for the input value.
        ActionResult axis2D(ActionComponent component, float scale = 1.0f, float threshold = 0.0f);

    private:
        ActionMap<ActionEnum> &actionMap;
        ActionBinding<ActionEnum> binding{};

        ActionResult add(ActionTrigger trigger, ActionComponent component, float scale, float threshold, float holdSeconds, float doubleTapSeconds);
    };

    /// @brief Maps physical input controls to typed actions and evaluates one frame at a time.
    /// @tparam ActionEnum Contiguous action enum whose values are used as indices in the range [0, actionCount).
    template <typename ActionEnum> class ActionMap
    {
    public:
        /// @brief Creates an action map with the requested number of action slots.
        /// @param actionCount Number of action slots; ActionEnum values passed to this map must be below it.
        explicit ActionMap(ActionEnum actionCount);

        /// @brief Resizes the action and state slots without clearing stored bindings.
        /// @param actionCount New number of action slots.
        void resize(ActionEnum actionCount);
        /// @brief Returns the number of action slots currently allocated.
        std::size_t getActionCount() const;

        /// @brief Reports whether an action enum value maps to an allocated slot.
        /// @return True when action converts to an index within the allocated slots.
        bool isValidAction(ActionEnum action) const;

        /// @brief Selects the value-processing kind for one action slot.
        /// @return InvalidAction when action is outside the allocated slots.
        ActionResult defineAction(ActionEnum action, ActionKind kind);

        /// @brief Clears per-frame action outputs and input snapshots while retaining bindings and gesture continuity.
        /// @note Call once before evaluate() for each new frame.
        void advanceFrame();
        /// @brief Evaluates all bindings against one input snapshot.
        /// @param inputState Input state whose current-frame values and activations are consumed.
        /// @param deltaSeconds Elapsed frame time in seconds; negative values are treated as zero.
        /// @note Call advanceFrame() before evaluation so pressed/released and snapshot data belong to one frame.
        void evaluate(const Input::InputState &inputState, float deltaSeconds);

        /// @brief Returns whether captured text input is available.
        /// @return True if text was received during the last evaluation.
        bool hasTextInput() const;

        /// @brief Returns captured UTF-8 text input.
        /// @return Text received during the last evaluation; the view remains valid until the next advanceFrame() or evaluate().
        std::string_view getTextInputUtf8() const;

        /// @brief Returns whether captured mouse position is valid.
        /// @return True if the mouse position is known.
        bool hasMousePosition() const;

        /// @brief Returns captured mouse X position.
        /// @return Latest client-area x position; meaningful only when hasMousePosition() is true.
        int getMouseX() const;

        /// @brief Returns captured mouse Y position.
        /// @return Latest client-area y position; meaningful only when hasMousePosition() is true.
        int getMouseY() const;

        /// @brief Returns captured raw mouse X movement.
        /// @return Raw x movement from the last evaluation.
        int getMouseDeltaX() const;

        /// @brief Returns captured raw mouse Y movement.
        /// @return Raw y movement from the last evaluation.
        int getMouseDeltaY() const;

        /// @brief Returns captured mouse wheel movement.
        /// @param wheel Wheel axis to query.
        /// @return Wheel movement from the last evaluation, in the input backend's wheel units.
        float getMouseWheelDelta(Input::MouseWheel wheel) const;

        /// @brief Reports whether an action is active after the most recent evaluation.
        /// @return True while active; false for an invalid action.
        bool isDown(ActionEnum action) const;
        /// @brief Reports whether an action became active during the most recent evaluation.
        /// @return True when pressed; false for an invalid action.
        bool wasPressed(ActionEnum action) const;
        /// @brief Reports whether an action became inactive during the most recent evaluation.
        /// @return True when released; false for an invalid action.
        bool wasReleased(ActionEnum action) const;

        /// @brief Returns the current scalar value for an action.
        /// @return Current value, or zero for an invalid or non-scalar action.
        float getValue(ActionEnum action) const;
        /// @brief Returns the current X component for a 2D action.
        /// @return Current X value, or zero for an invalid or non-2D action.
        float getValueX(ActionEnum action) const;
        /// @brief Returns the current Y component for a 2D action.
        /// @return Current Y value, or zero for an invalid or non-2D action.
        float getValueY(ActionEnum action) const;

        /// @brief Returns the map's binding storage without copying.
        /// @return Read-only view invalidated by binding additions or removals.
        std::span<const ActionBinding<ActionEnum>> getBindings() const;

        /// @brief Starts a fluent binding builder for one action.
        /// @param action Action slot targeted by the builder.
        /// @return Builder that retains a reference to this map.
        ActionBindingBuilder<ActionEnum> bind(ActionEnum action);
        /// @brief Validates and stores one binding by value.
        /// @return Success, ConflictingBinding when the binding is usable but overlaps another binding, or a rejection result.
        ActionResult addBinding(const ActionBinding<ActionEnum> &binding);

        /// @brief Starts a stateful rebind capture.
        /// @param action Action slot to bind.
        /// @param options Rebinding behavior copied into outSession.
        /// @param outSession Session overwritten with the new capture state.
        /// @return Collecting on success, or an error result.
        RebindResult beginBindingCapture(ActionEnum action, const ActionRebindOptions &options, ActionRebindSession<ActionEnum> &outSession) const;

        /// @brief Updates a stateful rebind capture.
        /// @param inputState Raw input state to inspect for this frame.
        /// @param session Active session; remains active while the result is Collecting.
        /// @param outCapture Capture data overwritten on every call.
        /// @param cancelControls Controls that cancel capture before they can be captured.
        /// @param ignoredControls Controls ignored during capture.
        /// @return Capture result; Canceled and InvalidControl end the session.
        RebindResult updateBindingCapture(
            const Input::InputState &inputState,
            ActionRebindSession<ActionEnum> &session,
            ActionRebindCapture<ActionEnum> &outCapture,
            std::span<const Input::InputControl> cancelControls = {},
            std::span<const Input::InputControl> ignoredControls = {}) const;

        /// @brief Captures one binding from the current activations, preferring the newest eligible button or strongest eligible axis movement.
        /// @param action Action slot to bind.
        /// @param inputState Raw input state to inspect.
        /// @param options Rebinding behavior used to filter and construct the binding.
        /// @param outCapture Capture data overwritten on every call.
        /// @param cancelControls Controls that cancel capture before they can be captured.
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
        /// @param capture Captured binding whose result must be Captured.
        /// @return Result from binding validation/add; InvalidBinding when capture is incomplete.
        ActionResult applyCapturedBinding(const ActionRebindCapture<ActionEnum> &capture);
        /// @brief Removes every binding targeting one action.
        /// @param action Action slot whose bindings are removed; invalid actions are ignored.
        void clearBindings(ActionEnum action);
        /// @brief Removes all bindings and their runtime gesture state.
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
        std::uint64_t valueChangeSequence = 0;             // Monotonic order for value-source changes.
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

} // namespace GameWIP::Action

#include "action/internal/action.inl"
