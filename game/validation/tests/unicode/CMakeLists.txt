gamewip_add_test_module(
    NAME unicode
    SOURCES
        # Module entry points
        unicode_test.cpp
        unicode_test.h
        module.cpp
        # Codec and conversion suites
        utf8_test.inl
        utf16_test.inl
        conversion_test.inl
        scalar_round_trip_test.inl
        boundary_test.inl
        # Shared validation helpers
        unicode_test_helpers.inl
        # Grapheme fixtures and suites
        grapheme_fixture_helpers.inl
        grapheme_conformance_helpers.inl
        grapheme_test.inl
    LINK_LIBRARIES Unicode TestSupport
)
