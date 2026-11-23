/// @file grapheme_cursor.cpp
/// @brief Caller-backed grapheme-boundary cursor operations.

#include "unicode/unicode.h"
#include "unicode/internal/grapheme.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

namespace GameWIP::Unicode::Utf8
{
    /// ------------------------------------------------------------
    /// Grapheme cursor
    /// ------------------------------------------------------------

    Types::Utf8::GraphemeIndexResult GraphemeCursor::reset(std::string_view text, std::span<std::size_t> boundaryStorage) noexcept
    {
        clear();

        std::size_t requiredBoundaryCount = 0;

        if(!Internal::Grapheme::writeBoundaries(text, boundaryStorage, requiredBoundaryCount))
        {
            return {.outcome = Types::Utf8::GraphemeIndexOutcome::InvalidEncoding};
        }

        if(requiredBoundaryCount > boundaryStorage.size())
        {
            return {
                .requiredBoundaryCount = requiredBoundaryCount,
                .outcome = Types::Utf8::GraphemeIndexOutcome::DestinationTooSmall,
            };
        }

        boundaries_ = boundaryStorage.first(requiredBoundaryCount);
        currentBoundaryIndex_ = 0;
        return {
            .requiredBoundaryCount = requiredBoundaryCount,
            .outcome = Types::Utf8::GraphemeIndexOutcome::Indexed,
        };
    }

    void GraphemeCursor::clear() noexcept
    {
        boundaries_ = {};
        currentBoundaryIndex_ = 0;
    }

    bool GraphemeCursor::ready() const noexcept
    {
        return !boundaries_.empty();
    }

    std::size_t GraphemeCursor::byteOffset() const noexcept
    {
        return ready() ? boundaries_[currentBoundaryIndex_] : 0;
    }

    std::size_t GraphemeCursor::boundaryCount() const noexcept
    {
        return boundaries_.size();
    }

    Types::Utf8::BoundaryResult GraphemeCursor::seek(std::size_t byteOffset) noexcept
    {
        if(!ready())
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        const auto found = std::lower_bound(boundaries_.begin(), boundaries_.end(), byteOffset);

        if(found == boundaries_.end() || *found != byteOffset)
        {
            return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }
        currentBoundaryIndex_ = static_cast<std::size_t>(found - boundaries_.begin());
        return {.byteOffset = byteOffset, .outcome = Types::Utf8::BoundaryOutcome::Found};
    }

    Types::Utf8::BoundaryResult GraphemeCursor::next() noexcept
    {
        if(!ready())
        {
            return {.outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        if(currentBoundaryIndex_ + 1 >= boundaries_.size())
        {
            return {.byteOffset = byteOffset(), .outcome = Types::Utf8::BoundaryOutcome::AtEnd};
        }
        ++currentBoundaryIndex_;
        return {.byteOffset = byteOffset(), .outcome = Types::Utf8::BoundaryOutcome::Found};
    }

    Types::Utf8::BoundaryResult GraphemeCursor::previous() noexcept
    {
        if(!ready())
        {
            return {.outcome = Types::Utf8::BoundaryOutcome::InvalidOffset};
        }

        if(currentBoundaryIndex_ == 0)
        {
            return {.byteOffset = 0, .outcome = Types::Utf8::BoundaryOutcome::AtBeginning};
        }
        --currentBoundaryIndex_;
        return {.byteOffset = byteOffset(), .outcome = Types::Utf8::BoundaryOutcome::Found};
    }

    void GraphemeCursor::discardAfterCurrent() noexcept
    {
        if(ready())
        {
            boundaries_ = boundaries_.first(currentBoundaryIndex_ + 1);
        }
    }

} // namespace GameWIP::Unicode::Utf8
