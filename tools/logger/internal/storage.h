/// @file storage.h
/// @brief Queue entries, source metadata, and retained message storage.

#pragma once

#include "logger/internal/core_types.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Logger::Detail::Core
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

    /// ------------------------------------------------------------
    /// Queue and source state
    /// ------------------------------------------------------------

    struct QueuedLogEntry
    {
        LogLevel level = LogLevel::Info;
        bool usesRegisteredSource = false;
        SourceId sourceId = 0;
        InlineLogText<kInlineSourceCapacity> sourceText;
        DynamicLogText message;
    };

    struct PendingLogEntry
    {
        LogLevel level = LogLevel::Info;
        bool usesRegisteredSource = false;
        SourceId sourceId = 0;
        InlineLogText<kInlineSourceCapacity> sourceText;
        std::string_view message;
        bool alreadyTruncated = false;
    };

    struct QueueSlot
    {
        std::atomic<std::size_t> sequence{0};
        bool skip = false;
        QueuedLogEntry entry;
        QueueSlot() = default;
        QueueSlot(const QueueSlot &) = delete;
        QueueSlot &operator=(const QueueSlot &) = delete;
        QueueSlot(QueueSlot &&) = delete;
        QueueSlot &operator=(QueueSlot &&) = delete;
    };

    struct RegisteredSource
    {
        SourceId id = 0;
        std::string name;
        std::optional<Color> color;
        mutable std::atomic<bool> enabled{true};

        RegisteredSource() = default;
        RegisteredSource(SourceId source, std::string displayName, std::optional<Color> sourceColor, bool sourceEnabled)
            : id(source)
            , name(std::move(displayName))
            , color(sourceColor)
            , enabled(sourceEnabled)
        {
        }

        RegisteredSource(const RegisteredSource &other)
            : id(other.id)
            , name(other.name)
            , color(other.color)
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        RegisteredSource(RegisteredSource &&other) noexcept
            : id(other.id)
            , name(std::move(other.name))
            , color(other.color)
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        RegisteredSource &operator=(const RegisteredSource &other)
        {
            if(this != &other)
            {
                id = other.id;
                name = other.name;
                color = other.color;
                enabled.store(other.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            return *this;
        }

        RegisteredSource &operator=(RegisteredSource &&other) noexcept
        {
            if(this != &other)
            {
                id = other.id;
                name = std::move(other.name);
                color = other.color;
                enabled.store(other.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }

            return *this;
        }
    };

    struct SourceRegistry
    {
        std::vector<RegisteredSource> sources;
        std::vector<std::size_t> directSourceLookup;
        SourceId directSourceBase = 0;
    };

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    template <typename Value> using AtomicSharedPointer = std::atomic<std::shared_ptr<Value>>;
#else
    template <typename Value> class AtomicSharedPointer
    {
    public:
        [[nodiscard]] std::shared_ptr<Value> load(std::memory_order order) const
        {
            return std::atomic_load_explicit(&value_, order);
        }

        void store(std::shared_ptr<Value> value, std::memory_order order)
        {
            std::atomic_store_explicit(&value_, std::move(value), order);
        }

    private:
        std::shared_ptr<Value> value_;
    };
#endif

} // namespace GameWIP::Logger::Detail::Core
