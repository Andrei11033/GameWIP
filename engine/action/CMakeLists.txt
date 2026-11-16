add_library(Action STATIC)

add_subdirectory(core)
add_subdirectory(internal)

target_include_directories(Action PUBLIC "${PROJECT_SOURCE_DIR}/engine")

target_compile_features(Action PUBLIC cxx_std_23)

target_link_libraries(Action PUBLIC Input)
