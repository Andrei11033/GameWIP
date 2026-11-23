/// @file text_storage.h
/// @brief Inline and dynamic storage for retained Logger text.

#pragma once

#include "logger/internal/core_types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Inline retained text storage
    /// ------------------------------------------------------------

    /// @brief Returns the byte count that keeps a valid UTF-8 prefix when a borrowed view may end mid-scalar.
    /// @note Ordinary hot messages are valid UTF-8 by contract, so only the final scalar needs inspection here.
    [[nodiscard]] inline std::size_t completeUtf8TailBoundary(std::string_view text) noexcept
    {
        if(text.empty())
        {
            return 0;
        }
        std::size_t lead = text.size() - 1;
        std::size_t continuationCount = 0;

        while(lead > 0 && continuationCount < 3)
        {
            const auto value = static_cast<unsigned char>(text[lead]);

            if((value & 0xC0u) != 0x80u)
            {
                break;
            }

            --lead;
            ++continuationCount;
        }

        const auto first = static_cast<unsigned char>(text[lead]);
        std::size_t expected = 1;

        if(first >= 0xC2u && first <= 0xDFu)
        {
            expected = 2;
        }
        else if(first >= 0xE0u && first <= 0xEFu)
        {
            expected = 3;
        }
        else if(first >= 0xF0u && first <= 0xF4u)
        {
            expected = 4;
        }
        else
        {
            return text.size();
        }

        const std::size_t available = text.size() - lead;
        return available < expected ? lead : text.size();
    }

    template <std::size_t InlineCapacity> class InlineLogText
    {
    public:
        void assign(std::string_view text)
        {
            if(text.size() <= InlineCapacity)
            {
                if(heapActive_)
                {
                    heapText_.clear();
                }

                inlineSize_ = text.size();
                heapActive_ = false;

                if(!text.empty())
                {
                    std::ranges::copy(text, inlineText_.begin());
                }

                return;
            }
            heapText_.assign(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }

        void assign(std::string &&text)
        {
            if(text.size() <= InlineCapacity)
            {
                assign(std::string_view(text));
                return;
            }
            heapText_ = std::move(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }

        void assignJoined(std::string_view first, std::string_view second)
        {
            first = first.substr(0, completeUtf8TailBoundary(first));
            const std::size_t total = first.size() + second.size();

            if(total <= InlineCapacity)
            {
                if(heapActive_)
                {
                    heapText_.clear();
                }

                inlineSize_ = total;
                heapActive_ = false;

                if(!first.empty())
                {
                    std::ranges::copy(first, inlineText_.begin());
                }

                if(!second.empty())
                {
                    std::ranges::copy(second, std::span{inlineText_}.subspan(first.size(), second.size()).begin());
                }

                return;
            }

            heapText_.clear();
            heapText_.reserve(total);
            heapText_.append(first);
            heapText_.append(second);
            inlineSize_ = 0;
            heapActive_ = true;
        }

        void transferFrom(InlineLogText &source)
        {
            if(source.heapActive_)
            {
                if(heapActive_)
                {
                    heapText_.clear();
                }

                heapText_.swap(source.heapText_);
                inlineSize_ = 0;
                heapActive_ = true;

                source.heapText_.clear();
                source.inlineSize_ = 0;
                source.heapActive_ = false;
                return;
            }
            assign(source.view());
            source.clear();
        }

        void clear(bool releaseHeapCapacity = false)
        {
            if(heapActive_)
            {
                heapText_.clear();
            }

            if(releaseHeapCapacity)
            {
                std::string{}.swap(heapText_);
            }

            inlineSize_ = 0;
            heapActive_ = false;
        }

        [[nodiscard]] std::string_view view() const
        {
            return heapActive_ ? std::string_view(heapText_) : std::string_view(inlineText_.data(), inlineSize_);
        }

        [[nodiscard]] std::size_t capacityBytes() const
        {
            static const std::size_t emptyCapacity = std::string{}.capacity();
            return heapText_.capacity() > emptyCapacity ? heapText_.capacity() : 0;
        }

    private:
        std::array<char, InlineCapacity> inlineText_{};
        std::size_t inlineSize_ = 0;
        bool heapActive_ = false;
        std::string heapText_;
    };

    /// ------------------------------------------------------------
    /// Message storage
    /// ------------------------------------------------------------

    class DynamicLogText
    {
    public:
        void configureInlineStorage(std::span<char> storage)
        {
            inlineText_ = storage;
            inlineSize_ = 0;
            clear();
        }

        void assign(std::string_view text)
        {
            if(text.size() <= inlineText_.size())
            {
                if(heapActive_)
                {
                    heapText_.clear();
                }

                inlineSize_ = text.size();

                if(!text.empty())
                {
                    std::ranges::copy(text, inlineText_.begin());
                }

                heapActive_ = false;
                return;
            }
            heapText_.assign(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }

        void assign(std::string &&text)
        {
            if(text.size() <= inlineText_.size())
            {
                assign(std::string_view(text));
                return;
            }
            heapText_ = std::move(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }

        void assignJoined(std::string_view first, std::string_view second)
        {
            first = first.substr(0, completeUtf8TailBoundary(first));
            const std::size_t total = first.size() + second.size();

            if(total <= inlineText_.size())
            {
                if(heapActive_)
                {
                    heapText_.clear();
                }

                inlineSize_ = total;

                if(!first.empty())
                {
                    std::ranges::copy(first, inlineText_.begin());
                }

                if(!second.empty())
                {
                    std::ranges::copy(second, inlineText_.subspan(first.size()).begin());
                }

                heapActive_ = false;
                return;
            }

            heapText_.clear();
            heapText_.reserve(total);
            heapText_.append(first.data(), first.size());
            heapText_.append(second.data(), second.size());
            inlineSize_ = 0;
            heapActive_ = true;
        }

        void transferFrom(DynamicLogText &source)
        {
            if(source.heapActive_)
            {
                if(heapActive_)
                {
                    heapText_.clear();
                }

                heapText_.swap(source.heapText_);
                inlineSize_ = 0;
                heapActive_ = true;

                source.heapText_.clear();
                source.inlineSize_ = 0;
                source.heapActive_ = false;
                return;
            }
            assign(source.view());
            source.clear();
        }

        void clear(bool releaseHeapCapacity = false)
        {
            if(heapActive_)
            {
                heapText_.clear();
            }

            if(releaseHeapCapacity)
            {
                std::string{}.swap(heapText_);
            }

            inlineSize_ = 0;
            heapActive_ = false;
        }

        [[nodiscard]] std::string_view view() const
        {
            if(heapActive_)
            {
                return heapText_;
            }

            return inlineSize_ == 0 ? std::string_view{} : std::string_view(inlineText_.data(), inlineSize_);
        }

        [[nodiscard]] std::size_t capacityBytes() const
        {
            static const std::size_t emptyCapacity = std::string{}.capacity();
            return heapText_.capacity() > emptyCapacity ? heapText_.capacity() : 0;
        }

    private:
        std::span<char> inlineText_;
        std::size_t inlineSize_ = 0;
        bool heapActive_ = false;
        std::string heapText_;
    };
} // namespace GameWIP::Logger::Internal::Core
