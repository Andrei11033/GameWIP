/// @file accessibility_publication.h
/// @brief Private text-cache and semantic-diff helpers.

#pragma once

#include "desktop/internal/accessibility_state.h"

#include <memory>
#include <string>
#include <string_view>

namespace GameWIP::Desktop::Internal::Helpers
{
    namespace A = Types::Accessibility;

    /// ------------------------------------------------------------
    /// Published text caches and notifications
    /// ------------------------------------------------------------

    [[nodiscard]] std::u16string toUtf16(std::string_view text);
    void indexAccessibilityText(const A::Node &node, AccessibilityTextCache &cache);
    void queueAccessibilityDiff(
        AccessibilityRuntime &runtime,
        const std::shared_ptr<const PublishedSnapshot> &before,
        const std::shared_ptr<const PublishedSnapshot> &after);

} // namespace GameWIP::Desktop::Internal::Helpers
