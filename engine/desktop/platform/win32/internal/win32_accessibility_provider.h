/// @file win32_accessibility_provider.h
/// @brief Private snapshot-backed UI Automation COM implementation contracts.

#pragma once

#include "desktop/internal/accessibility_state.h"
#include "desktop/platform/win32/internal/win32_accessibility.h"
#include <uiautomation.h>
#include <map>

namespace GameWIP::Desktop::Detail::Platform
{
    struct UiaState;
    class NodeProvider;
    class TextRangeProvider;

    // ------------------------------------------------------------
    // Native snapshot retention and bounded provider state
    // ------------------------------------------------------------

    struct UiaSnapshot final : AccessibilityNativeSnapshot
    {
        struct Layout
        {
            A::Rect bounds;
            std::optional<A::Rect> clip;
            bool visible = true;
            std::uint32_t order = 0, ordinal = 0;
            std::uint32_t subtreeEnd = 0;
            A::NodeId selectionContainer = 0, containingGrid = 0;
        };
        std::vector<IRawElementProviderSimple *> providers;
        std::vector<Layout> layout;
        std::vector<std::pair<std::size_t, std::size_t>> annotationSources;

        ~UiaSnapshot() override;
    };

    struct UiaQuery
    {
        std::shared_ptr<AccessibilityRuntime> runtime;
        std::shared_ptr<const PublishedSnapshot> snapshot;
        const A::Node *node = nullptr;
        std::size_t index = 0;
        std::optional<AccessibilityHostGeometry> host; // One coherent host state retained for the entire query.

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return node != nullptr;
        }
        [[nodiscard]] const AccessibilityTextCache &text() const noexcept
        {
            return snapshot->textCache[index];
        }
    };

    using RaiseNotification = HRESULT(WINAPI *)(IRawElementProviderSimple *, NotificationKind, NotificationProcessing, BSTR, BSTR);

    struct UiaState final : AccessibilityNativeState, std::enable_shared_from_this<UiaState>
    {
        std::map<A::NodeId, NodeProvider *> providers; // Mutated only under the portable publication gate; never used by queries.
        std::vector<std::unique_ptr<TextRangeProvider>> ranges;
        RaiseNotification raiseNotification = nullptr;
        std::atomic<std::uint32_t> listeners{0};

        ~UiaState() override;
        [[nodiscard]] HRESULT range(const UiaQuery &query, A::TextRange bounds, ITextRangeProvider **out) noexcept;
    };

    // ------------------------------------------------------------
    // Query projection and action transport
    // ------------------------------------------------------------

    [[nodiscard]] UiaQuery queryNode(const std::weak_ptr<AccessibilityRuntime> &runtime, A::NodeId id) noexcept;
    [[nodiscard]] IRawElementProviderSimple *providerFor(const PublishedSnapshot &snapshot, A::NodeId id) noexcept;
    [[nodiscard]] bool valueHidden(const A::Node &node) noexcept;
    [[nodiscard]] bool textHidden(const A::Node &node) noexcept;

    /// @brief Maps explicit read-only state and submitted editing support to native content editability.
    [[nodiscard]] bool contentReadOnly(const A::Node &node) noexcept;

    [[nodiscard]] UiaRect nodeBounds(const UiaQuery &query) noexcept;
    [[nodiscard]] UiaRect clientRect(const UiaQuery &query, A::Rect rect) noexcept;
    [[nodiscard]] HRESULT property(const UiaQuery &query, PROPERTYID id, VARIANT *out) noexcept;
    [[nodiscard]] HRESULT nodeArray(const PublishedSnapshot &snapshot, std::span<const A::NodeId> ids, SAFEARRAY **out) noexcept;
    [[nodiscard]] HRESULT action(const UiaQuery &query, A::ActionRequest request) noexcept;
    [[nodiscard]] HRESULT stringValue(std::u16string_view text, BSTR *out) noexcept;

    // ------------------------------------------------------------
    // COM pattern and provider interfaces
    // ------------------------------------------------------------

    // COM interfaces are destroyed by Release(), not through their intentionally nonvirtual base destructors.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
    /// @brief Separate identity-sharing Text adapter: collection selection and text selection have identical COM signatures.
    class TextPattern final : public ITextProvider2
    {
    public:
        explicit TextPattern(NodeProvider &owner) noexcept
            : owner_(owner)
        {
        }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple *child, ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection *out) override;
        HRESULT STDMETHODCALLTYPE RangeFromAnnotation(IRawElementProviderSimple *annotation, ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE GetCaretRange(BOOL *active, ITextRangeProvider **out) override;

    private:
        NodeProvider &owner_;
    };

    class NodeProvider final : public IRawElementProviderSimple,
                               public IRawElementProviderFragment,
                               public IRawElementProviderFragmentRoot,
                               public IRawElementProviderAdviseEvents,
                               public IInvokeProvider,
                               public IValueProvider,
                               public IRangeValueProvider,
                               public IToggleProvider,
                               public IExpandCollapseProvider,
                               public ISelectionProvider2,
                               public ISelectionItemProvider,
                               public IScrollProvider,
                               public IScrollItemProvider,
                               public IGridProvider,
                               public IGridItemProvider,
                               public ITableProvider,
                               public ITableItemProvider,
                               public IVirtualizedItemProvider,
                               public IItemContainerProvider,
                               public IAnnotationProvider,
                               public ITextChildProvider
    {
    public:
        NodeProvider(const std::shared_ptr<AccessibilityRuntime> &runtime, A::NodeId id, bool root) noexcept;
        [[nodiscard]] UiaQuery query() const noexcept;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *out) override;
        HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown **out) override;
        HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *out) override;
        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment **out) override;
        HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *out) override;
        HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE SetFocus() override;
        HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **out) override;
        HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment **out) override;
        HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **out) override;
        HRESULT STDMETHODCALLTYPE AdviseEventAdded(EVENTID id, SAFEARRAY *properties) override;
        HRESULT STDMETHODCALLTYPE AdviseEventRemoved(EVENTID id, SAFEARRAY *properties) override;
        HRESULT STDMETHODCALLTYPE Invoke() override;
        HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
        HRESULT STDMETHODCALLTYPE get_Value(BSTR *out) override;
        HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *out) override;
        HRESULT STDMETHODCALLTYPE SetValue(double value) override;
        HRESULT STDMETHODCALLTYPE get_Value(double *out) override;
        HRESULT STDMETHODCALLTYPE get_Maximum(double *out) override;
        HRESULT STDMETHODCALLTYPE get_Minimum(double *out) override;
        HRESULT STDMETHODCALLTYPE get_LargeChange(double *out) override;
        HRESULT STDMETHODCALLTYPE get_SmallChange(double *out) override;
        HRESULT STDMETHODCALLTYPE Toggle() override;
        HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState *out) override;
        HRESULT STDMETHODCALLTYPE Expand() override;
        HRESULT STDMETHODCALLTYPE Collapse() override;
        HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState *out) override;
        HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL *out) override;
        HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL *out) override;
        HRESULT STDMETHODCALLTYPE get_FirstSelectedItem(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_LastSelectedItem(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_CurrentSelectedItem(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_ItemCount(int *out) override;
        HRESULT STDMETHODCALLTYPE Select() override;
        HRESULT STDMETHODCALLTYPE AddToSelection() override;
        HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
        HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL *out) override;
        HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override;
        HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override;
        HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double *out) override;
        HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double *out) override;
        HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double *out) override;
        HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double *out) override;
        HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL *out) override;
        HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL *out) override;
        HRESULT STDMETHODCALLTYPE ScrollIntoView() override;
        HRESULT STDMETHODCALLTYPE GetItem(int row, int column, IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_RowCount(int *out) override;
        HRESULT STDMETHODCALLTYPE get_ColumnCount(int *out) override;
        HRESULT STDMETHODCALLTYPE get_Row(int *out) override;
        HRESULT STDMETHODCALLTYPE get_Column(int *out) override;
        HRESULT STDMETHODCALLTYPE get_RowSpan(int *out) override;
        HRESULT STDMETHODCALLTYPE get_ColumnSpan(int *out) override;
        HRESULT STDMETHODCALLTYPE get_ContainingGrid(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE GetRowHeaders(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE GetColumnHeaders(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE get_RowOrColumnMajor(RowOrColumnMajor *out) override;
        HRESULT STDMETHODCALLTYPE GetRowHeaderItems(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE GetColumnHeaderItems(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE Realize() override;
        HRESULT STDMETHODCALLTYPE
        FindItemByProperty(IRawElementProviderSimple *start, PROPERTYID id, VARIANT value, IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_AnnotationTypeId(int *out) override;
        HRESULT STDMETHODCALLTYPE get_AnnotationTypeName(BSTR *out) override;
        HRESULT STDMETHODCALLTYPE get_Author(BSTR *out) override;
        HRESULT STDMETHODCALLTYPE get_DateTime(BSTR *out) override;
        HRESULT STDMETHODCALLTYPE get_Target(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_TextContainer(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE get_TextRange(ITextRangeProvider **out) override;

    private:
        std::atomic<ULONG> references_{1};
        std::weak_ptr<AccessibilityRuntime> runtime_;
        A::NodeId id_;
        bool root_;
        TextPattern text_;
        ~NodeProvider() = default;
        [[nodiscard]] HRESULT request(A::ActionKind kind, A::SelectionOperation operation = A::SelectionOperation::Replace) noexcept;
        [[nodiscard]] HRESULT rangeValue(double A::RangeValue::*member, double *out) noexcept;
        [[nodiscard]] HRESULT scrollValue(double A::ScrollInfo::*member, double *out, bool horizontal) noexcept;
        [[nodiscard]] HRESULT collectionValue(std::uint32_t A::CollectionInfo::*member, int *out) noexcept;
        [[nodiscard]] HRESULT ancestor(bool selection, IRawElementProviderSimple **out) noexcept;
        [[nodiscard]] HRESULT headers(A::RelationKind kind, SAFEARRAY **out) noexcept;
        [[nodiscard]] HRESULT selectedItem(unsigned kind, IRawElementProviderSimple **out) noexcept;
    };

    class TextRangeProvider final : public ITextRangeProvider
    {
    public:
        std::atomic<ULONG> references{0};
        std::atomic<bool> available{true};
        std::shared_ptr<UiaState> lease;
        std::weak_ptr<AccessibilityRuntime> runtime;
        std::shared_ptr<const PublishedSnapshot> snapshot;
        A::NodeId node = 0;
        std::atomic<std::uint64_t> bounds{0};
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider *other, BOOL *out) override;
        HRESULT STDMETHODCALLTYPE
        CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider *other, TextPatternRangeEndpoint otherEndpoint, int *out) override;
        HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
        HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID id, VARIANT value, BOOL backward, ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider **out) override;
        HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID id, VARIANT *out) override;
        HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY **out) override;
        HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple **out) override;
        HRESULT STDMETHODCALLTYPE GetText(int maximumLength, BSTR *out) override;
        HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int *moved) override;
        HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int *moved) override;
        HRESULT STDMETHODCALLTYPE
        MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider *other, TextPatternRangeEndpoint otherEndpoint) override;
        HRESULT STDMETHODCALLTYPE Select() override;
        HRESULT STDMETHODCALLTYPE AddToSelection() override;
        HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
        HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL alignToTop) override;
        HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY **out) override;

    private:
        [[nodiscard]] UiaQuery query() const noexcept;
        [[nodiscard]] TextRangeProvider *peer(ITextRangeProvider *other) const noexcept;
        [[nodiscard]] HRESULT selection(A::SelectionOperation operation) noexcept;
    };
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace GameWIP::Desktop::Detail::Platform
