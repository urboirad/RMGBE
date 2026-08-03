cmake_minimum_required(VERSION 3.15)
project(RMGBE C)

set(CMAKE_C_STANDARD 11)

# Size optimizations
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG -fno-exceptions")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s -Wl,--gc-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-unwind-tables -fno-asynchronous-unwind-tables")

if(WIN32)
    add_executable(RMGBE
        main.c
        src/gap_buffer.c
        src/text_renderer.c
        src/editor.c
        src/file_panel.c
        src/terminal.c
        src/syntax.c
        src/update.c
        src/resources.rc
    )
else()
    add_executable(RMGBE
        main.c
        src/gap_buffer.c
        src/text_renderer.c
        src/editor.c
        src/file_panel.c
        src/terminal.c
        src/syntax.c
        src/update.c
    )
endif()

target_include_directories(RMGBE PRIVATE include vendor)

# Find GLFW
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    # Try pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW IMPORTED_TARGET glfw3)
    endif()
endif()

if(WIN32)
    if(TARGET glfw)
        target_link_libraries(RMGBE PRIVATE glfw)
    elseif(TARGET PkgConfig::GLFW)
        target_link_libraries(RMGBE PRIVATE PkgConfig::GLFW)
    else()
        # Fall back to vendored static lib
        target_link_directories(RMGBE PRIVATE ${CMAKE_SOURCE_DIR}/lib)
        target_link_libraries(RMGBE PRIVATE glfw3)
    endif()
    target_link_libraries(RMGBE PRIVATE opengl32 gdi32 ole32 comdlg32 shell32)
elseif(APPLE)
    if(TARGET glfw)
        target_link_libraries(RMGBE PRIVATE glfw)
    elseif(TARGET PkgConfig::GLFW)
        target_link_libraries(RMGBE PRIVATE PkgConfig::GLFW)
    endif()
    target_link_libraries(RMGBE PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
elseif(UNIX)
    if(TARGET glfw)
        target_link_libraries(RMGBE PRIVATE glfw)
    elseif(TARGET PkgConfig::GLFW)
        target_link_libraries(RMGBE PRIVATE PkgConfig::GLFW)
    endif()
    target_link_libraries(RMGBE PRIVATE GL m dl pthread)
endif()
