/// @file win32_accessibility_text_pattern.cpp
/// @brief Implements UI Automation text and Text2 pattern queries.

#include "desktop/platform/win32/internal/win32_accessibility_text_helpers.h"
#include "desktop/platform/win32/internal/win32_accessibility_provider.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace GameWIP::Desktop::Platform
{
    namespace
    {
        UiaQuery textQuery(NodeProvider &owner) noexcept
        {
            auto q = owner.query();

            if(q && (!q.node->text || textHidden(*q.node)))
            {
                q.node = nullptr;
            }

            return q;
        }

        HRESULT ranges(const UiaQuery &q, std::span<const A::TextRange> selection, SAFEARRAY **out) noexcept
        {
            if(!out)
            {
                return E_POINTER;
            }
            *out = nullptr;

            if(!q)
            {
                return kUnavailable;
            }

            auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selection.size()));

            if(!array)
            {
                return E_OUTOFMEMORY;
            }

            for(std::size_t i = 0; i < selection.size(); ++i)
            {
                ITextRangeProvider *range = nullptr;
                auto hr = makeTextRange(q, selection[i], &range);

                if(SUCCEEDED(hr))
                {
                    LONG index = static_cast<LONG>(i);
                    hr = SafeArrayPutElement(array, &index, range);
                    range->Release();
                }

                if(FAILED(hr))
                {
                    SafeArrayDestroy(array);
                    return hr;
                }
            }
            *out = array;
            return S_OK;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Text and Text2 pattern queries
    /// ------------------------------------------------------------

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
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = textQuery(owner_);

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.textContent().selection.empty())
        {
            return ranges(q, q.textContent().selection, out);
        }

        if(!q.textContent().caret)
        {
            return S_OK;
        }
        const A::TextRange caret{*q.textContent().caret, *q.textContent().caret};
        return ranges(q, {&caret, 1}, out);
    }

    HRESULT TextPattern::GetVisibleRanges(SAFEARRAY **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = textQuery(owner_);

        if(!q)
        {
            return kUnavailable;
        }

        std::size_t count = 0;

        for(const auto &fragment : q.textContent().fragments)
        {
            const auto r = clientRect(q, fragment.bounds);

            if(r.width > 0 && r.height > 0)
            {
                ++count;
            }
        }
        // Without layout the document range is the only meaningful range; do not fabricate per-line geometry.
        if(q.textContent().fragments.empty())
        {
            const auto r = nodeBounds(q);
            const A::TextRange whole{0, static_cast<std::uint32_t>(q.textContent().utf8.size())};
            return ranges(q, r.width > 0 && r.height > 0 ? std::span<const A::TextRange>(&whole, 1) : std::span<const A::TextRange>{}, out);
        }

        auto *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(count));

        if(!array)
        {
            return E_OUTOFMEMORY;
        }

        LONG index = 0;

        for(const auto &fragment : q.textContent().fragments)
        {
            const auto r = clientRect(q, fragment.bounds);

            if(r.width <= 0 || r.height <= 0)
            {
                continue;
            }
            ITextRangeProvider *range = nullptr;
            auto hr = makeTextRange(q, fragment.range, &range);

            if(SUCCEEDED(hr))
            {
                hr = SafeArrayPutElement(array, &index, range);
                range->Release();
                ++index;
            }

            if(FAILED(hr))
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
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = textQuery(owner_);

        if(!q)
        {
            return kUnavailable;
        }

        if(!std::isfinite(point.x) || !std::isfinite(point.y))
        {
            return E_INVALIDARG;
        }

        const A::TextFragment *best = nullptr;
        double distance = std::numeric_limits<double>::max();

        for(const auto &fragment : q.textContent().fragments)
        {
            const auto r = clientRect(q, fragment.bounds);

            if(r.width <= 0 || r.height <= 0)
            {
                continue;
            }
            const double x = std::clamp(point.x, r.left, r.left + r.width), y = std::clamp(point.y, r.top, r.top + r.height);
            const double candidate = std::hypot(point.x - x, point.y - y);

            if(candidate < distance)
            {
                best = &fragment;
                distance = candidate;
            }
        }

        if(!best)
        {
            return kUnsupported;
        }

        // The snapshot has fragment rectangles, not glyph advances. Fine-grained callers supply grapheme-sized fragments.
        const auto offset = best->range.begin;
        return makeTextRange(q, {offset, offset}, out);
    }

    HRESULT TextPattern::get_DocumentRange(ITextRangeProvider **out)
    {
        const auto q = textQuery(owner_);
        return makeTextRange(q, {0, q ? static_cast<std::uint32_t>(q.textContent().utf8.size()) : 0}, out);
    }

    HRESULT TextPattern::get_SupportedTextSelection(SupportedTextSelection *out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = SupportedTextSelection_None;
        const auto q = textQuery(owner_);

        if(!q)
        {
            return kUnavailable;
        }

        if(q.node->actions.contains(A::ActionKind::SetTextSelection) || !q.textContent().selection.empty() || q.textContent().caret)
        {
            *out = q.node->selection && q.node->selection->multiSelectable ? SupportedTextSelection_Multiple : SupportedTextSelection_Single;
        }

        return S_OK;
    }

    HRESULT TextPattern::RangeFromAnnotation(IRawElementProviderSimple *annotation, ITextRangeProvider **out)
    {
        if(!out)
        {
            return E_POINTER;
        }
        *out = nullptr;
        const auto q = textQuery(owner_);

        if(!q)
        {
            return kUnavailable;
        }

        if(!annotation)
        {
            return E_INVALIDARG;
        }

        for(const auto &a : q.textContent().annotations)
        {
            if(a.target != 0 && providerFor(*q.snapshot, a.target) == annotation)
            {
                return makeTextRange(q, a.range, out);
            }
        }

        return E_INVALIDARG;
    }

    HRESULT TextPattern::GetCaretRange(BOOL *active, ITextRangeProvider **out)
    {
        if(!active || !out)
        {
            return E_POINTER;
        }
        *active = FALSE;
        *out = nullptr;
        const auto q = textQuery(owner_);

        if(!q)
        {
            return kUnavailable;
        }

        if(!q.textContent().caret)
        {
            return kUnsupported;
        }

        *active = q.host && q.host->focused && q.node->states.contains(A::State::Focused);
        return makeTextRange(q, {*q.textContent().caret, *q.textContent().caret}, out);
    }
} // namespace GameWIP::Desktop::Platform
