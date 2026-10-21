cmake_minimum_required(VERSION 3.24)
project(WindowPiles LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the shipped product is a single exe with no redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /MP)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)

# ---------------------------------------------------------------------------
# The app (MVP): single exe, also runs as its own watchdog (--watchdog <pid>)
# ---------------------------------------------------------------------------
add_executable(WindowPiles WIN32
    src/app/main.cpp
    src/app/app.cpp
    src/app/app_icon.cpp
    src/common/win_util.cpp
    src/common/canvas.cpp
    src/common/json.cpp
    src/recovery/journal.cpp
    src/groups/pile_store.cpp
    src/preview/snapshot.cpp
    src/surface/pile_window.cpp
    src/interaction/drag_watcher.cpp
    src/ui/popup_base.cpp
    src/ui/menu.cpp
    src/ui/guide_window.cpp
    src/ui/pile_editor.cpp
    src/ui/inline_rename.cpp
    src/ui/work_frame.cpp)
target_include_directories(WindowPiles PRIVATE src)
target_link_libraries(WindowPiles PRIVATE user32 gdi32 dwmapi shell32 shcore advapi32 comdlg32 wtsapi32 kernel32 d3d11 dxgi windowsapp)

# ---------------------------------------------------------------------------
# P0 PoC helpers (screen sampling, window lookup, timing, OS info)
# ---------------------------------------------------------------------------
add_library(poc_common STATIC poc/common/poc_common.cpp)
target_include_directories(poc_common PUBLIC poc/common)
target_link_libraries(poc_common PUBLIC user32 gdi32 dwmapi psapi shell32 advapi32)

# Test window generator used by every PoC and later by integration tests.
add_executable(TestWindows tests/harness/TestWindows.cpp)
target_link_libraries(TestWindows PRIVATE poc_common)

# End-to-end check of WindowPiles.exe with injected mouse input.
add_executable(e2e_piles tests/e2e/e2e_piles.cpp)
target_link_libraries(e2e_piles PRIVATE poc_common)

# Restart / crash / logoff-with-frozen-app / reboot lifecycle check (no mouse input).
add_executable(e2e_lifecycle tests/e2e/e2e_lifecycle.cpp)
target_link_libraries(e2e_lifecycle PRIVATE poc_common)

# PoC-01/02: DWM thumbnails of other windows, incl. hidden/minimized sources.
add_executable(poc_dwm poc/poc01_dwm_thumbnail.cpp)
target_link_libraries(poc_dwm PRIVATE poc_common)

# PoC-03: WGC one-shot snapshot -> downscale -> hide -> show snapshot -> restore.
add_executable(poc_wgc poc/poc03_wgc_snapshot.cpp)
target_link_libraries(poc_wgc PRIVATE poc_common d3d11 dxgi windowsapp)

# PoC-04: recovery journal + watchdog process restoring hidden windows after a crash.
add_executable(poc_watchdog poc/poc04_watchdog.cpp)
target_link_libraries(poc_watchdog PRIVATE poc_common)

# PoC-05: window drag detection via WinEvent hooks + drop bar prototype.
add_executable(poc_drag poc/poc05_drag_probe.cpp)
target_link_libraries(poc_drag PRIVATE poc_common)
