cmake_minimum_required(VERSION 3.24)
project(WindowPiles LANGUAGES CXX RC)  # product name: Stack Neverflow (file names stay WindowPiles)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the shipped product is a single exe with no redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive-> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
                    $<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> $<$<COMPILE_LANGUAGE:CXX>:/MP>)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)

# ---------------------------------------------------------------------------
# The app (MVP): single exe, also runs as its own watchdog (--watchdog <pid>)
# ---------------------------------------------------------------------------
add_executable(WindowPiles WIN32
    src/app/app.rc
    src/app/main.cpp
    src/app/app.cpp
    src/app/app_icon.cpp
    src/common/win_util.cpp
    src/common/canvas.cpp
    src/common/json.cpp
    src/recovery/journal.cpp
    src/groups/pile_store.cpp
    src/groups/unread.cpp
    src/activity/activity_log.cpp
    src/preview/snapshot.cpp
    src/surface/pile_window.cpp
    src/interaction/drag_watcher.cpp
    src/ui/popup_base.cpp
    src/ui/menu.cpp
    src/ui/guide_window.cpp
    src/ui/pile_editor.cpp
    src/ui/inline_rename.cpp
    src/ui/stats_window.cpp
    src/ui/work_frame.cpp)
target_include_directories(WindowPiles PRIVATE src)
target_link_libraries(WindowPiles PRIVATE user32 gdi32 dwmapi shell32 shcore advapi32 comdlg32 wtsapi32 kernel32 d3d11 dxgi windowsapp)

# The exe's icon (Explorer, shortcuts, pinned items) is generated at build time from the same drawing
# code the app uses at runtime (DrawMark), so there is no binary .ico in the repo to keep in sync.
add_executable(make_icon tests/tools/make_icon.cpp src/app/app_icon.cpp src/common/canvas.cpp)
target_include_directories(make_icon PRIVATE src)
target_link_libraries(make_icon PRIVATE user32 gdi32 advapi32 ole32 windowscodecs)
set(APP_ICO "${CMAKE_BINARY_DIR}/gen/app.ico")
add_custom_command(OUTPUT "${APP_ICO}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/gen"
    COMMAND make_icon "${APP_ICO}"
    DEPENDS make_icon VERBATIM)
target_sources(WindowPiles PRIVATE "${APP_ICO}")
target_include_directories(WindowPiles PRIVATE "${CMAKE_BINARY_DIR}/gen")  # app.rc finds app.ico here
set_source_files_properties(src/app/app.rc PROPERTIES OBJECT_DEPENDS "${APP_ICO}")

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

# Notification badges on pile cards (no mouse input).
add_executable(e2e_badges tests/e2e/e2e_badges.cpp)
target_link_libraries(e2e_badges PRIVATE poc_common)

# Stacking order of windows in a pile and of overlapping cards is kept (no mouse input).
add_executable(e2e_order tests/e2e/e2e_order.cpp)
target_link_libraries(e2e_order PRIVATE poc_common)

# Load measurement: many big windows in one pile (no mouse input).
add_executable(e2e_perf tests/e2e/e2e_perf.cpp)
target_link_libraries(e2e_perf PRIVATE poc_common)

# Unit test: unread counts in window titles.
add_executable(unread_test tests/unit/unread_test.cpp src/groups/unread.cpp)
target_include_directories(unread_test PRIVATE src)

# Tool: renders the app mark and tray icons into one image for review (icon_preview out.bmp).
add_executable(icon_preview tests/tools/icon_preview.cpp src/app/app_icon.cpp src/common/canvas.cpp)
target_include_directories(icon_preview PRIVATE src)
target_link_libraries(icon_preview PRIVATE user32 gdi32 advapi32)

# Unit test: Canvas fast paths draw exactly what the plain per-pixel loops draw.
add_executable(canvas_test tests/unit/canvas_test.cpp src/common/canvas.cpp)
target_include_directories(canvas_test PRIVATE src)
target_link_libraries(canvas_test PRIVATE user32 gdi32)

# Unit test: activity log parsing and time-per-pile computation.
add_executable(activity_test tests/unit/activity_test.cpp src/activity/activity_log.cpp src/common/win_util.cpp)
target_include_directories(activity_test PRIVATE src)
target_link_libraries(activity_test PRIVATE shell32 shcore user32 advapi32)

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
