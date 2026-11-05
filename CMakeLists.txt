cmake_minimum_required(VERSION 3.15)
project(RMGBE C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
endforeach()

include(CTest)

# Size optimizations
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG -fno-exceptions")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s -Wl,--gc-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-unwind-tables -fno-asynchronous-unwind-tables")

set(RMGBE_CORE_SOURCES
    src/gap_buffer.c
    src/text.c
    src/undo.c
    src/file_io.c
    src/text_renderer.c
    src/editor.c
    src/file_panel.c
    src/terminal.c
    src/syntax.c
    src/theme.c
    src/startup_audio.c
)

if(WIN32)
    add_executable(RMGBE
        main.c
        ${RMGBE_CORE_SOURCES}
        vendor/tinyfiledialogs.c
        src/resources.rc
    )
else()
    add_executable(RMGBE
        main.c
        ${RMGBE_CORE_SOURCES}
        vendor/tinyfiledialogs.c
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
    target_link_libraries(RMGBE PRIVATE opengl32 gdi32 ole32 comdlg32 shell32 winmm)
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

# ---- Unit tests --------------------------------------------------------------
if(BUILD_TESTING)
    add_executable(rmgbe_tests
        tests/test_rmgbe.c
        tests/test_stubs.c
        src/gap_buffer.c
        src/text.c
        src/undo.c
        src/file_io.c
        src/editor.c
        src/syntax.c
    )
    target_include_directories(rmgbe_tests PRIVATE include vendor)
    if(UNIX)
        target_link_libraries(rmgbe_tests PRIVATE m)
    endif()
    add_test(NAME rmgbe_unit COMMAND rmgbe_tests)
endif()
