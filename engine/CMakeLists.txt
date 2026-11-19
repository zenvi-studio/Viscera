set(_viscera_log_level_default "Trace")
if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(_viscera_log_level_default "Info")
endif ()
set(VISCERA_LOG_MIN_LEVEL "${_viscera_log_level_default}" CACHE STRING
        "Compile-time logging floor (Trace, Debug, Info, Warning, Error, Fatal)")

if (VISCERA_LOG_MIN_LEVEL STREQUAL "Trace")
    set(VISCERA_LOG_MIN_LEVEL_VALUE 0)
elseif (VISCERA_LOG_MIN_LEVEL STREQUAL "Debug")
    set(VISCERA_LOG_MIN_LEVEL_VALUE 1)
elseif (VISCERA_LOG_MIN_LEVEL STREQUAL "Info")
    set(VISCERA_LOG_MIN_LEVEL_VALUE 2)
elseif (VISCERA_LOG_MIN_LEVEL STREQUAL "Warning")
    set(VISCERA_LOG_MIN_LEVEL_VALUE 3)
elseif (VISCERA_LOG_MIN_LEVEL STREQUAL "Error")
    set(VISCERA_LOG_MIN_LEVEL_VALUE 4)
elseif (VISCERA_LOG_MIN_LEVEL STREQUAL "Fatal")
    set(VISCERA_LOG_MIN_LEVEL_VALUE 5)
elseif (VISCERA_LOG_MIN_LEVEL MATCHES "^[0-9]+$")
    set(VISCERA_LOG_MIN_LEVEL_VALUE ${VISCERA_LOG_MIN_LEVEL})
else ()
    message(FATAL_ERROR "Invalid VISCERA_LOG_MIN_LEVEL=${VISCERA_LOG_MIN_LEVEL}")
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    option(VISCERA_LOG_DEBUG_SINKS "Compile debugger log sink" OFF)
else ()
    option(VISCERA_LOG_DEBUG_SINKS "Compile debugger log sink" ON)
endif ()

option(VISCERA_LOGGING_BENCHMARKS "Build logging microbenchmarks" OFF)

add_library(VisceraEngine
        src/Engine.cpp
        src/logging/Logger.cpp
        src/logging/Ring.cpp
        src/logging/ThreadId.cpp
        src/logging/ConsoleSink.cpp
        src/logging/FileSink.cpp
        src/logging/MemorySink.cpp
)

if (VISCERA_LOG_DEBUG_SINKS)
    target_sources(VisceraEngine PRIVATE src/logging/DebuggerSink.cpp)
    target_compile_definitions(VisceraEngine PUBLIC VISCERA_LOG_DEBUG_SINKS=1)
endif ()

add_library(Viscera::Engine ALIAS VisceraEngine)

file(GLOB_RECURSE VISCERA_ENGINE_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/viscera/Version.hpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/viscera/Version.hpp
        @ONLY
)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/viscera/logging/MinLevel.hpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/viscera/logging/MinLevel.hpp
        @ONLY
)

target_sources(VisceraEngine
        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
        FILES
        ${VISCERA_ENGINE_HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/include/viscera/Version.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/include/viscera/logging/MinLevel.hpp
)

target_include_directories(VisceraEngine
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(VisceraEngine
        PUBLIC cxx_std_23
)

set_target_properties(VisceraEngine PROPERTIES FOLDER "Engine")

#target_compile_definitions(VisceraEngine PUBLIC $<$<CONFIG:Debug>:VISCERA_ENABLE_ASSERTS>)

find_package(Threads REQUIRED)

target_link_libraries(VisceraEngine
        PUBLIC fmt::fmt Threads::Threads
        PRIVATE Viscera::Warnings
)

if (VISCERA_LOGGING_BENCHMARKS)
    add_executable(VisceraLoggingBench
            benchmarks/LoggingBench.cpp
    )
    target_link_libraries(VisceraLoggingBench
            PRIVATE
            Viscera::Engine
            Viscera::Warnings
    )
    set_target_properties(VisceraLoggingBench PROPERTIES FOLDER "Benchmarks")
endif ()
