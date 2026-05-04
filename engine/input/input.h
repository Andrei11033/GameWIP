#pragma once

#include <array>
#include <cstddef>

namespace GameWIP::Input
{
    // The keys we want to track.
    enum class Key
    {
        W,
        A,
        S,
        D,
        E,
        Q,
        UpArrow,
        LeftArrow,
        DownArrow,
        RightArrow,
        Space,
        Enter,
        Escape,
        Count
    };

    // Manages the state of input keys.
    class InputState
    {
    private:
        std::array<bool, static_cast<std::size_t>(Key::Count)> currentKeys{};  // Current frame key states.
        std::array<bool, static_cast<std::size_t>(Key::Count)> previousKeys{}; // Previous frame key states.

    public:
        /// @brief Copies current key states into previous key states for the next frame.
        void advanceFrame();

        /// @brief Sets the state of a key (called by the input system).
        /// @param key The key to set.
        /// @param isDown The state of the key.
        void setKey(Key key, bool isDown);

        /// @brief Checks if a key is currently pressed.
        /// @param key The key to check.
        /// @return True if the key is pressed, false otherwise.
        bool isKeyDown(Key key) const;

        /// @brief Checks if a key was pressed this frame.
        /// @param key The key to check.
        /// @return True if the key was pressed, false otherwise.
        bool wasKeyPressed(Key key) const;

        /// @brief Checks if a key was released this frame.
        /// @param key The key to check.
        /// @return True if the key was released, false otherwise.
        bool wasKeyReleased(Key key) const;
    };
}