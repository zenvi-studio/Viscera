include(FetchContent)

# ============= Engine Core Dependencies (Always Required) =============

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 12.2.1
        SYSTEM
)

FetchContent_MakeAvailable(fmt)

# ============= Testing Dependencies =============

if (BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    # GoogleTest defaults to the static CRT on MSVC. Force the shared CRT so it
    # matches the dynamic runtime CMake uses for the rest of the build.
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    else ()
        unset(gtest_force_shared_crt CACHE)
    endif ()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.18.0
            SYSTEM
    )

    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)
endif ()
