/// @file desktop_accessibility.cpp
/// @brief Installed opt-in header and exported authoring/reader/facade linkage probe.
#include "desktop/accessibility.h"
#include <type_traits>
static_assert(std::is_nothrow_copy_constructible_v<GameWIP::Desktop::Accessibility::SnapshotReader>);
bool probeInstalledAccessibility();
bool probeInstalledAccessibility()
{
    namespace Desktop = GameWIP::Desktop;
    Desktop::Window window;
    Desktop::Accessibility::SnapshotBuilder builder;
    const auto generation = builder.setGeneration(1);
    const auto root = builder.setRoot(1);
    Desktop::Types::Accessibility::Node node;
    node.id = 1;
    const auto added = builder.addNode(node);
    const auto valid = Desktop::Accessibility::validate(builder.view());
    const auto facade = window.accessibility();
    const auto reader = facade.readSnapshot();
    return generation.ok() && root.ok() && added.ok() && valid.ok() && !reader.isValid();
}
