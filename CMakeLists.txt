cmake_minimum_required(VERSION 3.30)

project(AtriNeuroIntegration LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ATRI Neuro Integration targets Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ATRI requires a 32-bit (Win32/x86) build. Configure with -A Win32.")
endif()

option(ATRI_BUILD_PROTOTYPE "Build the KiriKiri-Test integration prototype" OFF)

set(NEURO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(NEURO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NEURO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_PERMESSAGE_DEFLATE OFF CACHE BOOL "" FORCE)
add_subdirectory(libneurosdk EXCLUDE_FROM_ALL)

add_library(atri_version_proxy SHARED
    DLL-Proxy/dllmain.cpp
    DLL-Proxy/Source.def
)
set_target_properties(atri_version_proxy PROPERTIES
    OUTPUT_NAME version
)
target_compile_definitions(atri_version_proxy PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)
target_link_options(atri_version_proxy PRIVATE
    "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/DLL-Proxy/Source.def"
)

if(NOT ATRI_BUILD_PROTOTYPE)
    add_library(atri_neuro_integration SHARED
        Neuro-Integration/dllmain.cpp
    )
    set_target_properties(atri_neuro_integration PROPERTIES
        OUTPUT_NAME Neuro-Integration
    )
    target_compile_definitions(atri_neuro_integration PRIVATE
        UNICODE
        _UNICODE
    )
    target_link_libraries(atri_neuro_integration PRIVATE neurosdk)
endif()

if(ATRI_BUILD_PROTOTYPE)
    add_library(atri_neuro_prototype SHARED
        KiriKiri-Test/integration-test/dllmain.cpp
        KiriKiri-Test/integration-test/ContextMonitor.cpp
        KiriKiri-Test/integration-test/Logging.cpp
        KiriKiri-Test/integration-test/NeuroConsole.cpp
        KiriKiri-Test/integration-test/NeuroTCPServer.cpp
        KiriKiri-Test/integration-test/SelfLink.cpp
        KiriKiri-Test/integration-test/ScriptingWindow.cpp
        KiriKiri-Test/integration-test/TJSExecutor.cpp
        KiriKiri-Test/integration-test/neuro.def
        KiriKiri-Test/krkr-sdk/tp_stub.cpp
        KiriKiri-Test/ncbind/ncbind_auto.cpp
    )
    set_target_properties(atri_neuro_prototype PROPERTIES
        OUTPUT_NAME Neuro-Integration
    )
    target_compile_definitions(atri_neuro_prototype PRIVATE
        WIN32
        INTEGRATIONTEST_EXPORTS
        UNICODE
        _UNICODE
    )
    target_include_directories(atri_neuro_prototype PRIVATE
        KiriKiri-Test/krkr-sdk
        KiriKiri-Test/ncbind
    )
    target_link_libraries(atri_neuro_prototype PRIVATE
        neurosdk
    )
    target_link_options(atri_neuro_prototype PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/KiriKiri-Test/integration-test/neuro.def"
        /NODEFAULTLIB:LIBCMT
    )
endif()
