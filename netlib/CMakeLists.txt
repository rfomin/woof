include(WoofSettings)

add_library(netlib STATIC netlib.c netlib.h)
target_woof_settings(netlib)
target_include_directories(netlib INTERFACE ".")

if(WIN32)
    target_link_libraries(netlib ws2_32 iphlpapi)
endif()
