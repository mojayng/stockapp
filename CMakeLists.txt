cmake_minimum_required(VERSION 3.16)
project(stockapp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Turn this off (-DBUILD_GUI=OFF) to build only the engine, demos and tests,
# for example on a machine without GLFW.
option(BUILD_GUI "Build the ImGui/ImPlot simulation window" ON)

find_package(Threads REQUIRED)

# ------------------------------------------------------------------ engine library
# Everything except the programs' main() functions.
add_library(engine_core STATIC
  Order.cpp
  Orderbook.cpp
  Engine.cpp
  PriceFeed.cpp
)
target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC Threads::Threads)
target_compile_options(engine_core PRIVATE -Wall -Wextra)

# ------------------------------------------------------------------ console programs
add_executable(book_demo main.cpp)            # scripted order book walkthrough
target_link_libraries(book_demo PRIVATE engine_core)

add_executable(live_demo live_demo.cpp)       # engine thread + bot price feed, console output
target_link_libraries(live_demo PRIVATE engine_core)

# ------------------------------------------------------------------ tests
enable_testing()
add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE engine_core)
add_test(NAME unit_tests COMMAND tests)

# ------------------------------------------------------------------ simulation window
if(BUILD_GUI)
  include(FetchContent)
  # Downloaded once into build/_deps the first time you configure (needs internet).
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(imgui implot)

  find_package(glfw3 REQUIRED)
  find_package(OpenGL REQUIRED)

  # ImGui and ImPlot don't ship a CMake build, so compile their sources ourselves.
  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
  )
  target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR})
  target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

  add_executable(stockapp_gui gui_main.cpp)
  target_link_libraries(stockapp_gui PRIVATE engine_core imgui_lib)
  target_compile_options(stockapp_gui PRIVATE -Wall -Wextra)

  # On Windows, make sure this links as a normal console program (entry point main(),
  # not WinMain) so terminal messages stay visible and the link doesn't fail.
  if(WIN32)
    target_link_options(stockapp_gui PRIVATE -mconsole)
  endif()
endif()