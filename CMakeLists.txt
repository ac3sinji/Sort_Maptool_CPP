cmake_minimum_required(VERSION 3.20)
project(WaterSortMapTool CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.0
)
FetchContent_MakeAvailable(imgui)

# SDL2
find_package(SDL2 CONFIG REQUIRED)

add_executable(watersort
  WIN32
  src/main.cpp
  src/core/Types.hpp
  src/core/State.hpp
  src/core/State.cpp
  src/core/Generator.hpp
  src/core/Generator.cpp
  src/core/Solver.hpp
  src/core/Solver.cpp
  src/io/Csv.hpp
  src/io/Csv.cpp
  src/ui/App.hpp
  src/ui/App.cpp
)

# ImGui backends
target_sources(watersort PRIVATE
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# Includes
target_include_directories(watersort PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# Link
target_link_libraries(watersort PRIVATE SDL2::SDL2 SDL2::SDL2main)

# Copy SDL2 DLL next to the exe after build (VS2022 will then run without PATH tweaks)
add_custom_command(TARGET watersort POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:watersort>)