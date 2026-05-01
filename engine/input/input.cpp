#include "input.h"

namespace GameWIP::Input
{
    namespace
    {
        /// @brief Converts a Key enum value to its corresponding index.
        /// @param key The key to convert.
        /// @param outIndex The output index corresponding to the key.
        /// @return True if the conversion was successful, false if the key is out of range.
        bool tryGetKeyIndex(Key key, std::size_t &outIndex)
        {
            int keyIndex = static_cast<int>(key);
            int keyCount = static_cast<int>(Key::Count);

            if (keyIndex < 0 || keyIndex >= keyCount)
            {
                return false;
            }

            outIndex = static_cast<std::size_t>(keyIndex);
            return true;
        }
    }

    void InputState::advanceFrame()
    {
        previousKeys = currentKeys; // Copy current key states to previous for the next frame.
    }

    void InputState::setKey(Key key, bool isDown)
    {
        std::size_t keyIndex = 0;
        if (tryGetKeyIndex(key, keyIndex))
        {
            currentKeys[keyIndex] = isDown;
        }
    }

    bool InputState::isKeyDown(Key key) const
    {
        std::size_t keyIndex = 0;
        if (tryGetKeyIndex(key, keyIndex))
        {
            return currentKeys[keyIndex];
        }
        return false; // Invalid key, treat as not pressed.
    }

    bool InputState::wasKeyPressed(Key key) const
    {
        std::size_t keyIndex = 0;
        if (tryGetKeyIndex(key, keyIndex))
        {
            return currentKeys[keyIndex] && !previousKeys[keyIndex];
        }
        return false; // Invalid key, treat as not pressed.
    }

    bool InputState::wasKeyReleased(Key key) const
    {
        std::size_t keyIndex = 0;
        if (tryGetKeyIndex(key, keyIndex))
        {
            return !currentKeys[keyIndex] && previousKeys[keyIndex];
        }
        return false; // Invalid key, treat as not released.
    }
}