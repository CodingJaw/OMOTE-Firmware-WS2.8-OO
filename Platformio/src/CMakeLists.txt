# Source selection for mixed Arduino + ESP-IDF OMOTE environments.
# ESP-IDF ignores PlatformIO build_src_filter. Keep every ordinary OMOTE
# target on main.cpp; only explicit receipt test CMake flags change sources.
#
# Do not select tests via a C++ -D compile flag here: CMake needs to choose
# source files before compilation. The two options are mutually exclusive.
if(OMOTE_RECEIPT_DEVICE_LAB AND OMOTE_RECEIPT_COMPILE_PROBE)
    message(FATAL_ERROR "OMOTE receipt test modes are mutually exclusive")
endif()

if(OMOTE_RECEIPT_DEVICE_LAB)
    # Standalone USB serial-only cryptographic diagnostic; no OMOTE main.
    set(app_sources "${CMAKE_CURRENT_LIST_DIR}/receipt_mbedtls_device_lab.cpp")
elseif(OMOTE_RECEIPT_COMPILE_PROBE)
    # Normal OMOTE main with uncalled mbedTLS receipt compile/link probe.
    set(app_sources
        "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/receipt_mbedtls_compile_probe.cpp"
    )
else()
    # Preserve all existing firmware environments (Rev1/Rev5/Waveshare).
    set(app_sources "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
endif()

# The isolated diagnostic must not pull in OMOTE's managed network/storage
# components. Preserve pre-existing component requirements for regular OMOTE
# builds and add mbedTLS only to the opt-in compiled verifier.
if(OMOTE_RECEIPT_DEVICE_LAB)
    idf_component_register(
        SRCS ${app_sources}
        REQUIRES mbedtls
    )
elseif(OMOTE_RECEIPT_COMPILE_PROBE)
    idf_component_register(
        SRCS ${app_sources}
        REQUIRES mbedtls espressif__esp_websocket_client espressif__mdns joltwallet__littlefs
    )
else()
    idf_component_register(
        SRCS ${app_sources}
        REQUIRES espressif__esp_websocket_client espressif__mdns joltwallet__littlefs
    )
endif()
