target_sources(Input PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/win32_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/win32_input.h"
)
target_link_libraries(Input PRIVATE cfgmgr32 hid setupapi)

