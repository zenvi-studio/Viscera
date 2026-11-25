add_executable(VisceraEngineTests
        src/EngineTests.cpp
        src/logging/LoggerTests.cpp
        src/error/ErrorTests.cpp
        src/error/FormatTests.cpp
        src/error/ResultTests.cpp
        src/error/PanicTests.cpp
        src/error/AssertTests.cpp
        src/error/AssertDisabledTests.cpp
        src/error/LoggingOwnershipTests.cpp
        compile/log_format_ok.cpp
        compile/log_format_ok.hpp
        compile/error_standalone.cpp
)

target_link_libraries(VisceraEngineTests
        PRIVATE
        Viscera::Engine
        Viscera::Warnings
        GTest::gtest_main
)

target_include_directories(VisceraEngineTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/compile
)

set_target_properties(VisceraEngineTests PROPERTIES FOLDER "Tests")

gtest_discover_tests(VisceraEngineTests PROPERTIES RUN_SERIAL TRUE)

add_executable(VisceraLogFloorInfoTests
        src/logging/FloorInfoTests.cpp
)

target_link_libraries(VisceraLogFloorInfoTests
        PRIVATE
        Viscera::Engine
        Viscera::Warnings
        GTest::gtest_main
)

set_target_properties(VisceraLogFloorInfoTests PROPERTIES FOLDER "Tests")

gtest_discover_tests(VisceraLogFloorInfoTests PROPERTIES RUN_SERIAL TRUE)

function(viscera_add_must_not_compile target_name source test_name)
    add_library(${target_name} OBJECT ${source})
    target_link_libraries(${target_name}
            PRIVATE
            Viscera::Engine
            Viscera::Warnings
    )
    set_target_properties(${target_name} PROPERTIES
            EXCLUDE_FROM_ALL TRUE
            EXCLUDE_FROM_DEFAULT_BUILD TRUE
            FOLDER "Tests"
    )
    add_test(
            NAME ${test_name}
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target_name}
    )
    set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
endfunction()

viscera_add_must_not_compile(
        VisceraLogFormatMismatch
        compile/log_format_mismatch.cpp
        Logging.FormatMismatch.MustNotCompile
)
viscera_add_must_not_compile(
        VisceraErrorDiscardedResult
        compile/error_discarded_result.cpp
        Error.DiscardedResult.MustNotCompile
)
viscera_add_must_not_compile(
        VisceraErrorFormatMismatch
        compile/error_format_mismatch.cpp
        Error.FormatMismatch.MustNotCompile
)

add_test(
        NAME Error.IncludeDirection
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_error_includes.cmake
)
