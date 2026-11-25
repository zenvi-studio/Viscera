if (NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif ()

set(violations "")

function(viscera_reject_include file pattern label)
    file(STRINGS "${file}" lines)
    set(line_number 0)
    foreach (line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        if (line MATCHES "^[ \t]*#[ \t]*include[ \t]+[<\"']([^\"'>]+)[\"'>]")
            set(header "${CMAKE_MATCH_1}")
            string(REPLACE "\\" "/" header "${header}")
            if (header MATCHES "${pattern}")
                set(violations "${violations}${label}: ${file}:${line_number} includes ${header}\n" PARENT_SCOPE)
            endif ()
        endif ()
    endforeach ()
endfunction()

function(viscera_reject_tree directory pattern label)
    file(GLOB_RECURSE files LIST_DIRECTORIES false
            "${directory}/*.hpp"
            "${directory}/*.cpp"
            "${directory}/*.h")
    foreach (file IN LISTS files)
        viscera_reject_include("${file}" "${pattern}" "${label}")
    endforeach ()
    set(violations "${violations}" PARENT_SCOPE)
endfunction()

viscera_reject_tree(
        "${SOURCE_DIR}/engine/include/viscera/logging"
        "viscera/error(/|$)"
        "logging headers")
viscera_reject_tree(
        "${SOURCE_DIR}/engine/src/logging"
        "viscera/error(/|$)"
        "logging sources")
viscera_reject_tree(
        "${SOURCE_DIR}/engine/include/viscera/error"
        "viscera/logging(/|$)"
        "error headers")
viscera_reject_tree(
        "${SOURCE_DIR}/engine/src/error"
        "viscera/logging(/|$)"
        "error sources")
viscera_reject_include(
        "${SOURCE_DIR}/engine/include/viscera/core/FormatString.hpp"
        "viscera/logging(/|$)|viscera/error(/|$)"
        "FormatString")
viscera_reject_include(
        "${SOURCE_DIR}/engine/tests/compile/error_standalone.cpp"
        "viscera/logging(/|$)"
        "error standalone compile test")

if (NOT violations STREQUAL "")
    message(FATAL_ERROR "Forbidden error-module includes:\n${violations}")
endif ()
