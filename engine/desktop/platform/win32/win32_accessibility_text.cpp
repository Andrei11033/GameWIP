/// @file win32_accessibility_text.cpp
/// @brief Bounded, read-only UIA text adapters and preallocated range objects.

#include "desktop/platform/win32/internal/win32_accessibility_provider.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace GameWIP::Desktop::Detail::Platform
{
    namespace
    {
        constexpr HRESULT kUnavailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
        constexpr HRESULT kUnsupported = static_cast<HRESULT>(UIA_E_NOTSUPPORTED);
        // ------------------------------------------------------------
        // Range offsets and native arrays
        // ------------------------------------------------------------

        std::uint64_t pack(A::TextRange range) noexcept
        {
            return (static_cast<std::uint64_t>(range.begin) << 32) | range.end;
        }

        A::TextRange unpack(std::uint64_t value) noexcept
        {
            return {static_cast<std::uint32_t>(value >> 32), static_cast<std::uint32_t>(value)};
        }

        bool validEndpoint(TextPatternRangeEndpoint endpoint) noexcept
        {
            return endpoint == TextPatternRangeEndpoint_Start || endpoint == TextPatternRangeEndpoint_End;
        }

        std::uint32_t endpointOffset(A::TextRange range, TextPatternRangeEndpoint endpoint) noexcept
        {
            return endpoint == TextPatternRangeEndpoint_Start ? range.begin : range.end;
        }

        std::uint32_t wideOffset(const AccessibilityTextCache &cache, std::uint32_t offset) noexcept
        {
            const auto position = std::ranges::lower_bound(cache.scalars, offset);
            return cache.utf16Offsets[static_cast<std::size_t>(position - cache.scalars.begin())];
        }

        bool overlap(A::TextRange range, A::TextRange other) noexcept
        {
            return range.begin == range.end ? range.begin >= other.begin && range.begin < other.end
                                            : range.begin < other.end && other.begin < range.end;
        }

        UiaQuery textQuery(NodeProvider &owner) noexcept
        {
            auto q = owner.query();
            if (q && (!q.node->text || textHidden(*q.node)))
                q.node = nullptr;
            return q;
        }

        HRESULT makeRange(const UiaQuery &q, A::TextRange range, ITextRangeProvider **out) noexcept
        {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (!q)
                return kUnavailable;
            return static_cast<UiaState *>(q.runtime->native.get())->range(q, range, out);
        }

        HRESULT ranges(const UiaQuery &q, std::span<const A::TextRange> selection, SAFEARRAY **out) noexcept
        {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (!q)
                return kUnavailable;
            auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selection.size()));
            if (!array)
                return E_OUTOFMEMORY;
            for (std::size_t i = 0; i < selection.size(); ++i)
            {
                ITextRangeProvider *range = nullptr;
                auto hr = makeRange(q, selection[i], &range);
                if (SUCCEEDED(hr))
                {
                    LONG index = static_cast<LONG>(i);
                    hr = SafeArrayPutElement(array, &index, range);
                    range->Release();
                }
                if (FAILED(hr))
                {
                    SafeArrayDestroy(array);
                    return hr;
                }
            }
            *out = array;
            return S_OK;
        }
        // ------------------------------------------------------------
        // Text units and effective attributes
        // ------------------------------------------------------------

        std::span<const std::uint32_t> unitIndex(const UiaQuery &q, TextUnit unit, std::array<std::uint32_t, 2> &document) noexcept
        {
            document = {0, static_cast<std::uint32_t>(q.node->text->utf8.size())};
            switch (unit)
            {
            case TextUnit_Character:
                return q.text().graphemes;
            case TextUnit_Word:
                return q.text().words;
            case TextUnit_Line:
            case TextUnit_Paragraph:
                return q.text().lines;
            case TextUnit_Format:
                return q.text().formats;
            case TextUnit_Page:
            case TextUnit_Document:
                return document;
            }
            return {};
        }

        std::size_t unitAt(std::span<const std::uint32_t> units, std::uint32_t offset) noexcept
        {
            const auto it = std::ranges::upper_bound(units, offset);
            const auto index = static_cast<std::size_t>(it - units.begin());
            return index == 0 ? 0 : std::min(index - 1, units.size() - 1);
        }

        HRESULT reserved(VARIANT *out, bool mixed) noexcept
        {
            auto hr = mixed ? UiaGetReservedMixedAttributeValue(&out->punkVal) : UiaGetReservedNotSupportedValue(&out->punkVal);
            if (SUCCEEDED(hr))
                out->vt = VT_UNKNOWN;
            return hr;
        }

        /// @brief Projects one attribute over a span with no internal annotation boundary.
        HRESULT attributeSegment(const UiaQuery &q, A::TextRange bounds, TEXTATTRIBUTEID id, VARIANT *out) noexcept
        {
            VariantInit(out);
            if (id == UIA_IsReadOnlyAttributeId)
            {
                out->vt = VT_BOOL;
                out->boolVal = contentReadOnly(*q.node) ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            if (id == UIA_IsHiddenAttributeId)
            {
                out->vt = VT_BOOL;
                out->boolVal = q.node->states.contains(A::State::Invisible) ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            if (id == UIA_TextFlowDirectionsAttributeId)
            {
                if (q.node->text->direction == A::TextDirection::Mixed)
                    return reserved(out, true);
                if (q.node->text->direction == A::TextDirection::Unspecified)
                    return reserved(out, false);
                out->vt = VT_I4;
                out->lVal = q.node->text->direction == A::TextDirection::RightToLeft ? 1 : 0;
                return S_OK;
            }
            if (id != UIA_CultureAttributeId && id != UIA_IsItalicAttributeId && id != UIA_FontWeightAttributeId &&
                id != UIA_AnnotationTypesAttributeId && id != UIA_AnnotationObjectsAttributeId)
                return reserved(out, false);
            const A::TextAnnotation *selected = nullptr;
            std::size_t annotationIndex = 0;
            for (std::size_t i = 0; i < q.node->text->annotations.size(); ++i)
            {
                const auto &annotation = q.node->text->annotations[i];
                const bool relevant =
                    (id == UIA_CultureAttributeId && annotation.kind == A::TextAnnotationKind::Language) ||
                    (id == UIA_IsItalicAttributeId && annotation.kind == A::TextAnnotationKind::Emphasis && annotation.value == "italic") ||
                    (id == UIA_FontWeightAttributeId && annotation.kind == A::TextAnnotationKind::Emphasis && annotation.value == "bold") ||
                    (id == UIA_AnnotationTypesAttributeId && annotation.kind == A::TextAnnotationKind::Spelling) ||
                    (id == UIA_AnnotationObjectsAttributeId && annotation.target != 0);
                if (!relevant || !overlap(bounds, annotation.range))
                    continue;
                if (bounds.begin < annotation.range.begin || bounds.end > annotation.range.end)
                    return reserved(out, true);
                if (selected)
                {
                    const bool same = id == UIA_CultureAttributeId
                                          ? LocaleNameToLCID(reinterpret_cast<LPCWSTR>(q.text().annotationValues[annotationIndex].c_str()), 0) ==
                                                LocaleNameToLCID(reinterpret_cast<LPCWSTR>(q.text().annotationValues[i].c_str()), 0)
                                          : selected->kind == annotation.kind && selected->value == annotation.value &&
                                                (id != UIA_AnnotationObjectsAttributeId || selected->target == annotation.target);
                    if (!same)
                        return reserved(out, true);
                }
                selected = &annotation;
                annotationIndex = i;
            }
            if (id == UIA_CultureAttributeId)
            {
                const auto &language = selected                         ? q.text().annotationValues[annotationIndex]
                                       : !q.text().textLanguage.empty() ? q.text().textLanguage
                                                                        : q.text().language;
                if (language.empty())
                    return reserved(out, false);
                out->vt = VT_I4;
                out->lVal = static_cast<LONG>(LocaleNameToLCID(reinterpret_cast<LPCWSTR>(language.c_str()), 0));
                return S_OK;
            }
            if (id == UIA_IsItalicAttributeId)
            {
                out->vt = VT_BOOL;
                out->boolVal = selected && selected->value == "italic" ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            if (id == UIA_FontWeightAttributeId)
            {
                out->vt = VT_I4;
                out->lVal = selected && selected->value == "bold" ? 700 : 400;
                return S_OK;
            }
            if (id == UIA_AnnotationObjectsAttributeId)
            {
                const std::span<const A::NodeId> ids = selected ? std::span<const A::NodeId>(&selected->target, 1) : std::span<const A::NodeId>{};
                const auto hr = nodeArray(*q.snapshot, ids, &out->parray);
                if (SUCCEEDED(hr))
                    out->vt = VT_ARRAY | VT_UNKNOWN;
                return hr;
            }
            auto *array = SafeArrayCreateVector(VT_I4, 0, selected ? 1U : 0U);
            if (!array)
                return E_OUTOFMEMORY;
            if (selected)
            {
                LONG index = 0, type = AnnotationType_SpellingError;
                const auto hr = SafeArrayPutElement(array, &index, &type);
                if (FAILED(hr))
                {
                    SafeArrayDestroy(array);
                    return hr;
                }
            }
            out->vt = VT_ARRAY | VT_I4;
            out->parray = array;
            return S_OK;
        }

        bool attributeEquals(const VARIANT &a, const VARIANT &b) noexcept
        {
            if (a.vt != b.vt)
                return false;
            switch (a.vt)
            {
            case VT_BOOL:
                return a.boolVal == b.boolVal;
            case VT_I4:
                return a.lVal == b.lVal;
            case VT_R8:
                return a.dblVal == b.dblVal;
            case VT_BSTR:
                return std::wstring_view(a.bstrVal, SysStringLen(a.bstrVal)) == std::wstring_view(b.bstrVal, SysStringLen(b.bstrVal));
            case VT_UNKNOWN:
                return a.punkVal == b.punkVal;
            default:
                return false;
            }
        }

        /// @brief Compares effective scalar attributes across cached annotation boundaries without allocating query indexes.
        HRESULT attribute(const UiaQuery &q, A::TextRange bounds, TEXTATTRIBUTEID id, VARIANT *out) noexcept
        {
            if ((id != UIA_CultureAttributeId && id != UIA_IsItalicAttributeId && id != UIA_FontWeightAttributeId) || bounds.begin == bounds.end)
                return attributeSegment(q, bounds, id, out);

            VariantInit(out);
            bool found = false;
            const auto &formats = q.text().formats;
            auto position = std::ranges::upper_bound(formats, bounds.begin);
            if (position != formats.begin())
                --position;

            for (; position != formats.end() && position + 1 != formats.end() && *position < bounds.end; ++position)
            {
                const A::TextRange segment{std::max(*position, bounds.begin), std::min(*(position + 1), bounds.end)};
                if (segment.begin >= segment.end)
                    continue;

                VARIANT candidate{};
                const auto hr = attributeSegment(q, segment, id, &candidate);
                if (FAILED(hr))
                {
                    VariantClear(&candidate);
                    VariantClear(out);
                    return hr;
                }

                if (!found)
                {
                    *out = candidate;
                    VariantInit(&candidate);
                    found = true;
                }
                else
                {
                    const bool equal = attributeEquals(*out, candidate);
                    VariantClear(&candidate);
                    if (!equal)
                    {
                        VariantClear(out);
                        return reserved(out, true);
                    }
                }
            }

            return found ? S_OK : attributeSegment(q, bounds, id, out);
        }
    } // namespace

    // ------------------------------------------------------------
    // Bounded text-range allocation
    // ------------------------------------------------------------

    HRESULT UiaState::range(const UiaQuery &q, A::TextRange rangeBounds, ITextRangeProvider **out) noexcept
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!q || !q.runtime->live.load())
            return kUnavailable;
        for (auto &slot : ranges)
        {
            bool expected = true;
            if (!slot->available.compare_exchange_strong(expected, false))
                continue;
            slot->lease = shared_from_this();
            slot->runtime = q.runtime;
            slot->snapshot = q.snapshot;
            slot->node = q.node->id;
            slot->bounds.store(pack(rangeBounds));
            slot->references.store(1);
            *out = slot.get();
            return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    }
    // ------------------------------------------------------------
    // Text and Text2 pattern queries
    // ------------------------------------------------------------

    HRESULT TextPattern::QueryInterface(REFIID iid, void **out)
    {
        return owner_.QueryInterface(iid, out);
    }

    ULONG TextPattern::AddRef()
    {
        return owner_.AddRef();
    }

    ULONG TextPattern::Release()
    {
        return owner_.Release();
    }

    HRESULT TextPattern::GetSelection(SAFEARRAY **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = textQuery(owner_);
        if (!q)
            return kUnavailable;
        if (!q.node->text->selection.empty())
            return ranges(q, q.node->text->selection, out);
        if (!q.node->text->caret)
            return S_OK;
        const A::TextRange caret{*q.node->text->caret, *q.node->text->caret};
        return ranges(q, {&caret, 1}, out);
    }

    HRESULT TextPattern::GetVisibleRanges(SAFEARRAY **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = textQuery(owner_);
        if (!q)
            return kUnavailable;
        std::size_t count = 0;
        for (const auto &fragment : q.node->text->fragments)
        {
            const auto r = clientRect(q, fragment.bounds);
            if (r.width > 0 && r.height > 0)
                ++count;
        }
        // Without layout the document range is the only meaningful range; do not fabricate per-line geometry.
        if (q.node->text->fragments.empty())
        {
            const auto r = nodeBounds(q);
            const A::TextRange whole{0, static_cast<std::uint32_t>(q.node->text->utf8.size())};
            return ranges(q, r.width > 0 && r.height > 0 ? std::span<const A::TextRange>(&whole, 1) : std::span<const A::TextRange>{}, out);
        }
        auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(count));
        if (!array)
            return E_OUTOFMEMORY;
        LONG index = 0;
        for (const auto &fragment : q.node->text->fragments)
        {
            const auto r = clientRect(q, fragment.bounds);
            if (r.width <= 0 || r.height <= 0)
                continue;
            ITextRangeProvider *range = nullptr;
            auto hr = makeRange(q, fragment.range, &range);
            if (SUCCEEDED(hr))
            {
                hr = SafeArrayPutElement(array, &index, range);
                range->Release();
                ++index;
            }
            if (FAILED(hr))
            {
                SafeArrayDestroy(array);
                return hr;
            }
        }
        *out = array;
        return S_OK;
    }

    HRESULT TextPattern::RangeFromChild(IRawElementProviderSimple *child, ITextRangeProvider **out)
    {
        return RangeFromAnnotation(child, out);
    }

    HRESULT TextPattern::RangeFromPoint(UiaPoint point, ITextRangeProvider **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = textQuery(owner_);
        if (!q)
            return kUnavailable;
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return E_INVALIDARG;
        const A::TextFragment *best = nullptr;
        double distance = std::numeric_limits<double>::max();
        for (const auto &fragment : q.node->text->fragments)
        {
            const auto r = clientRect(q, fragment.bounds);
            if (r.width <= 0 || r.height <= 0)
                continue;
            const double x = std::clamp(point.x, r.left, r.left + r.width), y = std::clamp(point.y, r.top, r.top + r.height);
            const double candidate = std::hypot(point.x - x, point.y - y);
            if (candidate < distance)
            {
                best = &fragment;
                distance = candidate;
            }
        }
        if (!best)
            return kUnsupported;
        // The snapshot has fragment rectangles, not glyph advances. Fine-grained callers supply grapheme-sized fragments.
        const auto offset = best->range.begin;
        return makeRange(q, {offset, offset}, out);
    }

    HRESULT TextPattern::get_DocumentRange(ITextRangeProvider **out)
    {
        const auto q = textQuery(owner_);
        return makeRange(q, {0, q ? static_cast<std::uint32_t>(q.node->text->utf8.size()) : 0}, out);
    }

    HRESULT TextPattern::get_SupportedTextSelection(SupportedTextSelection *out)
    {
        if (!out)
            return E_POINTER;
        *out = SupportedTextSelection_None;
        const auto q = textQuery(owner_);
        if (!q)
            return kUnavailable;
        if (q.node->actions.contains(A::ActionKind::SetTextSelection) || !q.node->text->selection.empty() || q.node->text->caret)
            *out = q.node->selection && q.node->selection->multiSelectable ? SupportedTextSelection_Multiple : SupportedTextSelection_Single;
        return S_OK;
    }

    HRESULT TextPattern::RangeFromAnnotation(IRawElementProviderSimple *annotation, ITextRangeProvider **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = textQuery(owner_);
        if (!q)
            return kUnavailable;
        if (!annotation)
            return E_INVALIDARG;
        for (const auto &a : q.node->text->annotations)
            if (a.target != 0 && providerFor(*q.snapshot, a.target) == annotation)
                return makeRange(q, a.range, out);
        return E_INVALIDARG;
    }

    HRESULT TextPattern::GetCaretRange(BOOL *active, ITextRangeProvider **out)
    {
        if (!active || !out)
            return E_POINTER;
        *active = FALSE;
        *out = nullptr;
        const auto q = textQuery(owner_);
        if (!q)
            return kUnavailable;
        if (!q.node->text->caret)
            return kUnsupported;
        *active = q.host && q.host->focused && q.node->states.contains(A::State::Focused);
        return makeRange(q, {*q.node->text->caret, *q.node->text->caret}, out);
    }

    // ------------------------------------------------------------
    // Retained range identity and comparisons
    // ------------------------------------------------------------

    UiaQuery TextRangeProvider::query() const noexcept
    {
        auto q = queryNode(runtime, node);
        // Immutable ranges never silently reinterpret byte offsets against a newer document.
        if (q && (!q.node->text || textHidden(*q.node) || q.snapshot->storage.generation != snapshot->storage.generation))
            q.node = nullptr;
        return q;
    }

    TextRangeProvider *TextRangeProvider::peer(ITextRangeProvider *other) const noexcept
    {
        if (!other || !lease)
            return nullptr;
        for (const auto &slot : lease->ranges)
            if (static_cast<ITextRangeProvider *>(slot.get()) == other && slot->node == node && slot->snapshot &&
                slot->snapshot->storage.generation == snapshot->storage.generation)
                return slot.get();
        return nullptr;
    }

    HRESULT TextRangeProvider::QueryInterface(REFIID iid, void **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, __uuidof(ITextRangeProvider)))
            return E_NOINTERFACE;
        *out = static_cast<ITextRangeProvider *>(this);
        AddRef();
        return S_OK;
    }

    ULONG TextRangeProvider::AddRef()
    {
        return references.fetch_add(1) + 1;
    }

    ULONG TextRangeProvider::Release()
    {
        const auto remaining = references.fetch_sub(1) - 1;
        if (remaining != 0)
            return remaining;
        auto keepAlive = std::move(lease);
        snapshot.reset();
        runtime.reset();
        node = 0;
        bounds.store(0);
        available.store(true);
        return 0;
    }

    HRESULT TextRangeProvider::Clone(ITextRangeProvider **out)
    {
        return makeRange(query(), unpack(bounds.load()), out);
    }

    HRESULT TextRangeProvider::Compare(ITextRangeProvider *other, BOOL *out)
    {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        if (!query())
            return kUnavailable;
        if (!other)
            return E_INVALIDARG;
        if (const auto *range = peer(other))
            *out = bounds.load() == range->bounds.load();
        return S_OK;
    }

    HRESULT TextRangeProvider::CompareEndpoints(
        TextPatternRangeEndpoint endpoint,
        ITextRangeProvider *other,
        TextPatternRangeEndpoint target,
        int *out)
    {
        if (!out)
            return E_POINTER;
        *out = 0;
        if (!query())
            return kUnavailable;
        if (!validEndpoint(endpoint) || !validEndpoint(target))
            return E_INVALIDARG;
        const auto *range = peer(other);
        if (!range)
            return E_INVALIDARG;
        const auto a = endpointOffset(unpack(bounds.load()), endpoint), b = endpointOffset(unpack(range->bounds.load()), target);
        *out = a < b ? -1 : a > b ? 1 : 0;
        return S_OK;
    }
    // ------------------------------------------------------------
    // Range expansion and search
    // ------------------------------------------------------------

    HRESULT TextRangeProvider::ExpandToEnclosingUnit(TextUnit unit)
    {
        const auto q = query();
        if (!q)
            return kUnavailable;
        std::array<std::uint32_t, 2> document{};
        const auto units = unitIndex(q, unit, document);
        if (units.empty())
            return E_INVALIDARG;
        auto old = bounds.load();
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            const auto range = unpack(old);
            const auto start = std::min(unitAt(units, range.begin), units.size() > 1 ? units.size() - 2 : 0);
            const auto end = units.size() > 1 ? start + 1 : start;
            if (bounds.compare_exchange_weak(old, pack({units[start], units[end]})))
                return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }

    HRESULT TextRangeProvider::FindAttribute(TEXTATTRIBUTEID id, VARIANT value, BOOL backward, ITextRangeProvider **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (value.vt != VT_BOOL && value.vt != VT_I4 && value.vt != VT_R8 && value.vt != VT_BSTR)
            return kUnsupported;
        const auto range = unpack(bounds.load());
        const auto &formats = q.text().formats;
        bool found = false;
        A::TextRange match{};
        for (std::size_t i = 0; i + 1 < formats.size(); ++i)
        {
            const A::TextRange segment{std::max(formats[i], range.begin), std::min(formats[i + 1], range.end)};
            if (segment.begin >= segment.end)
                continue;
            VARIANT candidate{};
            const auto hr = attribute(q, segment, id, &candidate);
            const bool equal = SUCCEEDED(hr) && attributeEquals(candidate, value);
            VariantClear(&candidate);
            if (FAILED(hr))
                return hr;
            if (equal)
            {
                if (!found || backward)
                    match = segment;
                found = true;
                if (!backward)
                    break;
            }
        }
        return found ? makeRange(q, match, out) : S_OK;
    }

    HRESULT TextRangeProvider::FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        const auto length = SysStringLen(text);
        if (length == 0 || length > static_cast<UINT>(std::numeric_limits<int>::max()))
            return E_INVALIDARG;
        const auto range = unpack(bounds.load());
        const auto &cache = q.text();
        const auto beginWide = wideOffset(cache, range.begin), endWide = wideOffset(cache, range.end);
        if (length > endWide - beginWide)
            return S_OK;
        bool found = false;
        A::TextRange match{};
        for (auto it = std::ranges::lower_bound(cache.graphemes, range.begin); it != cache.graphemes.end() && *it <= range.end; ++it)
        {
            const auto start = wideOffset(cache, *it);
            if (start > endWide - length)
                break;
            if (CompareStringOrdinal(
                    reinterpret_cast<LPCWSTR>(std::u16string_view(cache.text).substr(start, length).data()),
                    static_cast<int>(length),
                    text,
                    static_cast<int>(length),
                    ignoreCase) != CSTR_EQUAL)
                continue;
            const auto endPosition = std::ranges::lower_bound(cache.utf16Offsets, start + length);
            if (endPosition == cache.utf16Offsets.end() || *endPosition != start + length)
                continue;
            const auto end = cache.scalars[static_cast<std::size_t>(endPosition - cache.utf16Offsets.begin())];
            if (!std::ranges::binary_search(cache.graphemes, end))
                continue;
            match = {*it, end};
            found = true;
            if (!backward)
                break;
        }
        return found ? makeRange(q, match, out) : S_OK;
    }
    // ------------------------------------------------------------
    // Range content and geometry
    // ------------------------------------------------------------

    HRESULT TextRangeProvider::GetAttributeValue(TEXTATTRIBUTEID id, VARIANT *out)
    {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        const auto q = query();
        return q ? attribute(q, unpack(bounds.load()), id, out) : kUnavailable;
    }

    HRESULT TextRangeProvider::GetBoundingRectangles(SAFEARRAY **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        const auto range = unpack(bounds.load());
        std::size_t count = 0;
        for (const auto &f : q.node->text->fragments)
            if (range.begin != range.end && overlap(range, f.range))
            {
                const auto r = clientRect(q, f.bounds);
                if (r.width > 0 && r.height > 0)
                    ++count;
            }
        if (count > static_cast<std::size_t>(LONG_MAX) / 4)
            return E_OUTOFMEMORY;
        auto *array = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(count * 4));
        if (!array)
            return E_OUTOFMEMORY;
        LONG index = 0;
        for (const auto &f : q.node->text->fragments)
            if (range.begin != range.end && overlap(range, f.range))
            {
                const auto r = clientRect(q, f.bounds);
                if (r.width <= 0 || r.height <= 0)
                    continue;
                // Fragment geometry is application-owned; never assume a proportional glyph layout.
                for (double value : {r.left, r.top, r.width, r.height})
                {
                    const auto hr = SafeArrayPutElement(array, &index, &value);
                    if (FAILED(hr))
                    {
                        SafeArrayDestroy(array);
                        return hr;
                    }
                    ++index;
                }
            }
        *out = array;
        return S_OK;
    }

    HRESULT TextRangeProvider::GetEnclosingElement(IRawElementProviderSimple **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        *out = providerFor(*q.snapshot, node);
        if (!*out)
            return kUnavailable;
        (*out)->AddRef();
        return S_OK;
    }

    HRESULT TextRangeProvider::GetText(int maximumLength, BSTR *out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (maximumLength < -1)
            return E_INVALIDARG;
        const auto range = unpack(bounds.load());
        const auto start = wideOffset(q.text(), range.begin), end = wideOffset(q.text(), range.end);
        auto length = end - start;
        if (maximumLength >= 0)
            length = std::min(length, static_cast<std::uint32_t>(maximumLength));
        // Do not return half a surrogate pair when the client supplies a UTF-16 length cap.
        if (length > 0 && start + length < end && q.text().text[start + length - 1] >= 0xD800 && q.text().text[start + length - 1] <= 0xDBFF)
            --length;
        return stringValue(std::u16string_view(q.text().text).substr(start, length), out);
    }
    // ------------------------------------------------------------
    // Range movement
    // ------------------------------------------------------------

    HRESULT TextRangeProvider::Move(TextUnit unit, int count, int *moved)
    {
        if (!moved)
            return E_POINTER;
        *moved = 0;
        const auto q = query();
        if (!q)
            return kUnavailable;
        std::array<std::uint32_t, 2> document{};
        const auto units = unitIndex(q, unit, document);
        if (units.empty())
            return E_INVALIDARG;
        auto old = bounds.load();
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            const auto range = unpack(old);
            const bool degenerate = range.begin == range.end;
            const auto maxIndex = units.size() > 1 ? units.size() - (degenerate ? 1U : 2U) : 0;
            const auto initial = std::min(unitAt(units, range.begin), maxIndex);
            const auto destination = std::clamp(static_cast<std::int64_t>(initial) + count, std::int64_t{0}, static_cast<std::int64_t>(maxIndex));
            const auto index = static_cast<std::size_t>(destination);
            const A::TextRange next{units[index], degenerate ? units[index] : units[std::min(index + 1, units.size() - 1)]};
            if (count == 0)
                return S_OK;
            if (bounds.compare_exchange_weak(old, pack(next)))
            {
                *moved = static_cast<int>(destination - static_cast<std::int64_t>(initial));
                return S_OK;
            }
        }
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }

    HRESULT TextRangeProvider::MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int *moved)
    {
        if (!moved)
            return E_POINTER;
        *moved = 0;
        const auto q = query();
        if (!q)
            return kUnavailable;
        if (!validEndpoint(endpoint))
            return E_INVALIDARG;
        std::array<std::uint32_t, 2> document{};
        const auto units = unitIndex(q, unit, document);
        if (units.empty())
            return E_INVALIDARG;
        auto old = bounds.load();
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            auto range = unpack(old);
            const auto offset = endpointOffset(range, endpoint);
            auto initial = static_cast<std::int64_t>(std::ranges::lower_bound(units, offset) - units.begin());
            if (count > 0 && initial < static_cast<std::int64_t>(units.size()) && units[static_cast<std::size_t>(initial)] != offset)
                --initial;
            const auto destination = std::clamp(initial + count, std::int64_t{0}, static_cast<std::int64_t>(units.size() - 1));
            if (count == 0)
                return S_OK;
            if (endpoint == TextPatternRangeEndpoint_Start)
            {
                range.begin = units[static_cast<std::size_t>(destination)];
                range.end = std::max(range.begin, range.end);
            }
            else
            {
                range.end = units[static_cast<std::size_t>(destination)];
                range.begin = std::min(range.begin, range.end);
            }
            if (bounds.compare_exchange_weak(old, pack(range)))
            {
                *moved = static_cast<int>(destination - initial);
                return S_OK;
            }
        }
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }

    HRESULT TextRangeProvider::MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider *other, TextPatternRangeEndpoint target)
    {
        if (!query())
            return kUnavailable;
        if (!validEndpoint(endpoint) || !validEndpoint(target))
            return E_INVALIDARG;
        const auto *range = peer(other);
        if (!range)
            return E_INVALIDARG;
        const auto offset = endpointOffset(unpack(range->bounds.load()), target);
        auto old = bounds.load();
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            auto next = unpack(old);
            if (endpoint == TextPatternRangeEndpoint_Start)
            {
                next.begin = offset;
                next.end = std::max(next.begin, next.end);
            }
            else
            {
                next.end = offset;
                next.begin = std::min(next.begin, next.end);
            }
            if (bounds.compare_exchange_weak(old, pack(next)))
                return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    // ------------------------------------------------------------
    // Selection and scrolling requests
    // ------------------------------------------------------------

    HRESULT TextRangeProvider::selection(A::SelectionOperation operation) noexcept
    {
        const auto q = query();
        if (!q)
            return kUnavailable;
        A::ActionRequest request;
        request.action = A::ActionKind::SetTextSelection;
        request.range = unpack(bounds.load());
        request.selectionOperation = operation;
        return action(q, std::move(request));
    }

    HRESULT TextRangeProvider::Select()
    {
        return selection(A::SelectionOperation::Replace);
    }

    HRESULT TextRangeProvider::AddToSelection()
    {
        return selection(A::SelectionOperation::Add);
    }

    HRESULT TextRangeProvider::RemoveFromSelection()
    {
        return selection(A::SelectionOperation::Remove);
    }

    HRESULT TextRangeProvider::ScrollIntoView(BOOL alignToTop)
    {
        const auto q = query();
        if (!q)
            return kUnavailable;
        A::ActionRequest request;
        request.action = A::ActionKind::ScrollTo;
        request.range = unpack(bounds.load());
        request.point.y = alignToTop ? 0 : 1;
        return action(q, std::move(request));
    }

    HRESULT TextRangeProvider::GetChildren(SAFEARRAY **out)
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        const auto q = query();
        if (!q)
            return kUnavailable;
        const auto range = unpack(bounds.load());
        std::size_t count = 0;
        for (const auto &a : q.node->text->annotations)
            if (a.target != 0 && overlap(range, a.range))
                ++count;
        auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(count));
        if (!array)
            return E_OUTOFMEMORY;
        LONG index = 0;
        for (const auto &a : q.node->text->annotations)
            if (a.target != 0 && overlap(range, a.range))
            {
                IUnknown *provider = providerFor(*q.snapshot, a.target);
                const auto hr = SafeArrayPutElement(array, &index, provider);
                if (FAILED(hr))
                {
                    SafeArrayDestroy(array);
                    return hr;
                }
                ++index;
            }
        *out = array;
        return S_OK;
    }
} // namespace GameWIP::Desktop::Detail::Platform
