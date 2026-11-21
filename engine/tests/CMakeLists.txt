add_executable(VisceraEngineTests
        src/EngineTests.cpp
        src/logging/LoggerTests.cpp
        compile/log_format_ok.cpp
        compile/log_format_ok.hpp
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

add_library(VisceraLogFormatMismatch OBJECT
        compile/log_format_mismatch.cpp
)
target_link_libraries(VisceraLogFormatMismatch
        PRIVATE
        Viscera::Engine
        Viscera::Warnings
)
set_target_properties(VisceraLogFormatMismatch PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        FOLDER "Tests"
)

add_test(
        NAME Logging.FormatMismatch.MustNotCompile
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target VisceraLogFormatMismatch
)
set_tests_properties(Logging.FormatMismatch.MustNotCompile PROPERTIES WILL_FAIL TRUE)
