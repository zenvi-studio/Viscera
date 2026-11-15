add_executable(VisceraEngineTests
        src/EngineTests.cpp
)

target_link_libraries(VisceraEngineTests
        PRIVATE
        Viscera::Engine
        Viscera::Warnings
        GTest::gtest_main
)

set_target_properties(VisceraEngineTests PROPERTIES FOLDER "Tests")

gtest_discover_tests(VisceraEngineTests)