# Enables AddressSanitizer only when explicitly requested and compile/link probing proves the selected toolchain supports it.
# A requested but unsupported sanitizer configuration fails at configure time with the owning environment guidance.

if(GAMEWIP_ENABLE_ADDRESS_SANITIZER)
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)

    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
    check_cxx_source_compiles("int main() { return 0; }" GAMEWIP_ADDRESS_SANITIZER_AVAILABLE)
    cmake_pop_check_state()

    if(NOT GAMEWIP_ADDRESS_SANITIZER_AVAILABLE)
        message(
            FATAL_ERROR
            "AddressSanitizer is unavailable with the selected compiler. "
            "On Windows, configure this preset using the MSYS2 CLANG64 environment."
        )
    endif()

    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
