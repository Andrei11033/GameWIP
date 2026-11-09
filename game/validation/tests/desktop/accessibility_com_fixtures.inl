/// @file accessibility_com_fixtures.inl
/// @brief Native COM fixtures shared by Desktop accessibility tests.

// ------------------------------------------------------------
// Native COM fixtures
// ------------------------------------------------------------

template <typename T> struct Com
{
    T *value = nullptr;
    Com() = default;

    explicit Com(T *source)
        : value(source)
    {
    }

    ~Com()
    {
        if(value)
        {
            value->Release();
        }
    }
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;

    T **put()
    {
        if(value)
        {
            value->Release();
        }
        value = nullptr;
        return &value;
    }

    explicit operator bool() const
    {
        return value != nullptr;
    }

    T *operator->() const
    {
        return value;
    }
};

template <typename T> bool pattern(Context &context, IRawElementProviderSimple *provider, PATTERNID id, Com<T> &out)
{
    Com<IUnknown> unknown;

    if(!context.expectTrue("pattern lookup succeeds", SUCCEEDED(provider->GetPatternProvider(id, unknown.put())) && static_cast<bool>(unknown)))
    {
        return false;
    }

    return context.expectTrue("pattern has correct interface", SUCCEEDED(unknown->QueryInterface(__uuidof(T), reinterpret_cast<void **>(out.put()))));
}
