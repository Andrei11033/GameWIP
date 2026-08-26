add_library(Action STATIC)

target_sources(Action PRIVATE core/action.cpp)

target_include_directories(Action PUBLIC "${PROJECT_SOURCE_DIR}/engine")

target_compile_features(Action PUBLIC cxx_std_23)

target_link_libraries(Action PUBLIC Input)
