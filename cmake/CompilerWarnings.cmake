# clang-cl speaks the MSVC frontend and rejects this file's GNU-style Clang flags.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(FATAL_ERROR
            "clang-cl is not supported (${CMAKE_CXX_COMPILER_ID} with the MSVC frontend). Supported compilers: Clang, GNU (g++), and MSVC (cl).")
endif ()

add_library(ProjectWarnings INTERFACE)
add_library(Viscera::Warnings ALIAS ProjectWarnings)

# Shared by Clang and GCC. Clang appends four options GCC does not accept cleanly.
set(viscera_gnu_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wcast-align
        -Wcast-qual
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wnull-dereference
        -Wextra-semi
        -Wzero-as-null-pointer-constant
        -Wmissing-noreturn
        -Wrange-loop-construct
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ProjectWarnings INTERFACE
            ${viscera_gnu_warnings}
            -Wunreachable-code
            -Wcomma
            -Wloop-analysis
            -Wmove
    )
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ProjectWarnings INTERFACE
            ${viscera_gnu_warnings}
    )
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ProjectWarnings INTERFACE
            /W4
            /permissive-
            /w44242
            /w44254
            /w44263
            /w44265
            /w44287
            /w44296
            /w44311
            /w44545
            /w44546
            /w44547
            /w44549
            /w44555
            /w44619
            /w44640
            /w44826
            /w44905
            /w44906
            /w44928
            /utf-8
    )
else ()
    message(FATAL_ERROR
            "Unsupported compiler '${CMAKE_CXX_COMPILER_ID}'. Supported compilers: Clang, GNU (g++), and MSVC (cl).")
endif ()

if (VISCERA_WARNINGS_AS_ERRORS)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif ()

# ============= Static Analysis =============

if (VISCERA_CLANG_TIDY)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING
                "VISCERA_CLANG_TIDY is ON, but clang-tidy runs only with Clang (found ${CMAKE_CXX_COMPILER_ID}). Skipping clang-tidy.")
    else ()
        find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
        if (CLANG_TIDY_EXECUTABLE)
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}")
            if (VISCERA_WARNINGS_AS_ERRORS)
                list(APPEND CMAKE_CXX_CLANG_TIDY "--warnings-as-errors=*")
            endif ()
        else ()
            message(WARNING "VISCERA_CLANG_TIDY is ON, but clang-tidy executable was not found.")
        endif ()
    endif ()
endif ()

if (VISCERA_CPPCHECK)
    find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)

    if (CPPCHECK_EXECUTABLE)
        # Build-time analysis
        set(CMAKE_CXX_CPPCHECK
                "${CPPCHECK_EXECUTABLE}"
                "--enable=warning,style,performance,portability"
                "--std=c++23"
                "--inline-suppr"
                "--quiet"
                "--template=gcc"
                "--suppressions-list=${PROJECT_SOURCE_DIR}/.cppcheck-suppressions"
        )

        if (VISCERA_WARNINGS_AS_ERRORS)
            list(APPEND CMAKE_CXX_CPPCHECK
                    "--error-exitcode=1"
            )
        endif ()

        set(CMAKE_C_CPPCHECK
                "${CMAKE_CXX_CPPCHECK}"
        )

        # Full-project analysis
        add_custom_target(VisceraCppcheck
                COMMAND
                "${CPPCHECK_EXECUTABLE}"
                "--project=${PROJECT_BINARY_DIR}/compile_commands.json"
                "--enable=warning,style,performance,portability"
                "--std=c++23"
                "--inline-suppr"
                "--suppressions-list=${PROJECT_SOURCE_DIR}/.cppcheck-suppressions"
                "--error-exitcode=1"
                "--quiet"
                "-i${PROJECT_BINARY_DIR}/_deps"

                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"

                COMMENT "Running full Viscera Cppcheck analysis"

                VERBATIM
        )
    else ()
        message(WARNING
                "VISCERA_CPPCHECK is ON, but cppcheck was not found."
        )
    endif ()
endif ()
