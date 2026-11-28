/// @file text.h
/// @brief Portable text and layout types.

#pragma once

#include "desktop/accessibility/geometry.h"
#include "desktop/accessibility/identity.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace GameWIP::Desktop::Types::Accessibility
{
    /// ------------------------------------------------------------
    /// Text content and layout
    /// ------------------------------------------------------------

    /// @brief UTF-8 byte range. End is exclusive.
    /// @details Ranges must begin and end at documented Unicode scalar and grapheme boundaries.
    struct TextRange
    {
        std::uint32_t begin = 0; ///< Inclusive UTF-8 byte offset at a grapheme boundary.
        std::uint32_t end = 0;   ///< Exclusive UTF-8 byte offset at a grapheme boundary; begin == end is a caret.
    };

    /// @brief Text reading direction.
    enum class TextDirection : std::uint8_t
    {
        Unspecified,
        LeftToRight,
        RightToLeft,
        Mixed
    };

    /// @brief Read-only text annotation kinds.
    enum class TextAnnotationKind : std::uint8_t
    {
        Language,
        Emphasis,
        Spelling,
        Link,
        Custom
    };

    /// @brief Annotation applied to one UTF-8 text range.
    struct TextAnnotation
    {
        TextRange range;                                      ///< Annotated UTF-8 byte range in the containing TextContent.
        TextAnnotationKind kind = TextAnnotationKind::Custom; ///< Typed annotation category.
        std::string_view value;                               ///< Borrowed strict UTF-8; language values reject embedded U+0000.
        NodeId target = kInvalidNodeId;                       ///< Optional semantic annotation/link node; nonzero references must exist.
    };

    /// @brief Optional geometry for one fragment of read-only text.
    struct TextFragment
    {
        TextRange range; ///< UTF-8 byte range covered by this application-supplied layout fragment.
        Rect bounds;     ///< Window-client logical bounds for the represented text fragment.
    };

    /// @brief Read-only UTF-8 text plus ranges, annotations, direction, and language metadata.
    struct TextContent
    {
        std::string_view utf8;                                ///< Borrowed strict UTF-8; embedded U+0000 is preserved.
        TextDirection direction = TextDirection::Unspecified; ///< Reading direction, not an inferred rendering layout.
        std::string_view language;                            ///< Borrowed language hint; empty inherits Node language; embedded U+0000 is invalid.
        std::span<const TextAnnotation> annotations;          ///< Borrowed bounded annotations; overlapping annotations are permitted.
        std::span<const TextFragment> fragments;              ///< Borrowed optional Window-client logical range geometry.
        std::span<const TextRange> selection;                 ///< Borrowed current text selections in UTF-8 byte offsets.
        std::optional<std::uint32_t> caret;                   ///< Optional current caret at a grapheme boundary.
    };
} // namespace GameWIP::Desktop::Types::Accessibility
