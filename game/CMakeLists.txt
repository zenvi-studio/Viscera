add_executable(VisceraGame
        src/main.cpp
)
target_link_libraries(VisceraGame
        PRIVATE Viscera::Engine Viscera::Warnings
)

set_target_properties(VisceraGame PROPERTIES FOLDER "Game")
