add_library(VisceraEngine
        src/Engine.cpp
)

add_library(Viscera::Engine ALIAS VisceraEngine)

file(GLOB_RECURSE VISCERA_ENGINE_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/viscera/Version.hpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/viscera/Version.hpp
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

target_link_libraries(VisceraEngine
        PRIVATE Viscera::Warnings
)
