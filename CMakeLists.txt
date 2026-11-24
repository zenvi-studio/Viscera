cmake_minimum_required(VERSION 3.28...4.4.5)
project(
        Viscera
        VERSION 0.0.1
        LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============[ IDE Folder Setup ]=============

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeInternal")

# Drop the duplicate option older revisions stored beside VISCERA_WARNINGS_AS_ERRORS.
unset(ENABLE_WARNINGS_AS_ERRORS CACHE)

option(VISCERA_CLANG_TIDY "Run clang-tidy during build" OFF)
option(VISCERA_CPPCHECK "Run cppcheck during build" OFF)
option(VISCERA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(VISCERA_BUILD_DOCS "Enable Doxygen documentation target" OFF)

# =============[ Testing Configuration ]=============

include(CTest)
if (BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    enable_testing()
endif ()

# =============[ Dependencies ]=============

get_property(BEFORE_DEPS GLOBAL PROPERTY BUILDSYSTEM_TARGETS)

# Earlier revisions stored this OFF in the cache for the whole project.
# Drop that entry, mute it only while fetched projects configure, then return
# to CMake's default so first-party deprecations stay visible.
unset(CMAKE_WARN_DEPRECATED CACHE)
unset(CMAKE_WARN_DEPRECATED)
set(CMAKE_WARN_DEPRECATED OFF)
include(cmake/Dependencies.cmake)
unset(CMAKE_WARN_DEPRECATED)

get_property(AFTER_DEPS GLOBAL PROPERTY BUILDSYSTEM_TARGETS)
foreach (target IN LISTS AFTER_DEPS)
    if (NOT target IN_LIST BEFORE_DEPS)
        set_target_properties(${target} PROPERTIES FOLDER "ThirdParty")
    endif ()
endforeach ()

# =============[ Compiler Configuration ]=============

include(cmake/CompilerWarnings.cmake)

# =============[ Project Modules ]=============

add_subdirectory(engine)
add_subdirectory(game)

# =============[ Module Tests ]=============

if (BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(engine/tests)
    #    add_subdirectory(game/tests)
endif ()

# =============[ Documentation ]=============

if (VISCERA_BUILD_DOCS AND PROJECT_IS_TOP_LEVEL)
    find_package(Doxygen REQUIRED OPTIONAL_COMPONENTS dot)

    if (DOXYGEN_FOUND)
        # Set Doxygen options via DOXYGEN_<OPTION> variables
        set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
        set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
        set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")

        # Input & Filtering: scan public headers + existing markdown docs, skip internal detail/
        set(DOXYGEN_RECURSIVE YES)
        set(DOXYGEN_EXCLUDE_PATTERNS "*/detail/*" "*/templates/*")
        set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/docs/README.md")

        # C++ & Diagram tuning
        set(DOXYGEN_EXTRACT_ALL NO)
        set(DOXYGEN_EXTRACT_STATIC YES)
        set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
        set(DOXYGEN_HAVE_DOT ${DOXYGEN_DOT_FOUND})
        set(DOXYGEN_UML_LOOK YES)
        set(DOXYGEN_CALL_GRAPH NO)
        set(DOXYGEN_CALLER_GRAPH NO)

        doxygen_add_docs(
                docs
                "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/docs"
                COMMENT "Generating Viscera API documentation with Doxygen"
        )

        set_target_properties(docs PROPERTIES FOLDER "Documentation")
    else ()
        message(STATUS "Doxygen not found; documentation target will not be generated.")
    endif ()
endif ()
