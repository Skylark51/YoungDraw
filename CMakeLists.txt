cmake_minimum_required(VERSION 3.20)
project(YoungDraw VERSION 0.4.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_library(youngdraw_core src/model.cpp src/structure_templates.cpp src/layout.cpp src/plot_data.cpp src/mechanism_data.cpp src/mechanism_graph.cpp src/mechanism_edit.cpp src/scene.cpp src/arrows.cpp)
target_include_directories(youngdraw_core PUBLIC src)
if(WIN32)
  add_library(youngdraw_storage src/win_storage.cpp src/win_projects.cpp)
  target_compile_definitions(youngdraw_storage PUBLIC UNICODE _UNICODE NOMINMAX)
  target_include_directories(youngdraw_storage PUBLIC src)
  add_library(youngdraw_cdxml src/cdxml.cpp src/cdx.cpp)
  target_link_libraries(youngdraw_cdxml PUBLIC youngdraw_core xmllite shlwapi ole32)
  target_compile_definitions(youngdraw_cdxml PUBLIC UNICODE _UNICODE NOMINMAX)
  target_link_libraries(youngdraw_storage PRIVATE youngdraw_cdxml)
  add_library(youngdraw_editor STATIC
    src/win_editor_state.cpp
    src/win_editor_chrome.cpp
    src/win_editor_images.cpp
    src/win_editor_animation.cpp
    src/win_editor_render.cpp
    src/win_editor_files.cpp
    src/win_editor_clipboard.cpp
    src/win_editor_templates.cpp
    src/win_editor_tabs.cpp
    src/win_editor_text.cpp
    src/win_editor_reaction.cpp
    src/win_editor_plot.cpp
    src/win_editor_structure.cpp
    src/win_editor_commands.cpp
    src/win_editor_canvas.cpp
    src/win_editor_pointer.cpp
    src/win_editor_shortcuts.cpp
    src/win_editor_window.cpp
    src/win_project_manager.cpp
    src/win_splash.cpp
    src/win_dialogs.cpp
    src/win_grid.cpp
    src/win_gallery.cpp
    src/office.cpp
    src/win_theme.cpp)
  target_link_libraries(youngdraw_editor PUBLIC youngdraw_storage youngdraw_cdxml
    gdiplus comdlg32 shell32 comctl32 oleaut32 uuid advapi32 dwmapi)
  add_executable(YoungDraw WIN32 src/winmain.cpp resources/youngdraw.rc)
  target_include_directories(YoungDraw PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  set_property(SOURCE resources/youngdraw.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/youngdraw.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/youngdraw-icon.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/backrooms.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/nostalgia.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/horror.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/themes/professor-watching.png")
  target_link_libraries(YoungDraw PRIVATE youngdraw_editor)
endif()
enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE youngdraw_core)
if(WIN32)
  target_link_libraries(core_tests PRIVATE youngdraw_cdxml)
  target_compile_definitions(core_tests PRIVATE TEST_CDXML)
endif()
add_test(NAME core_tests COMMAND core_tests)
if(WIN32)
  add_executable(file_save_tests tests/file_save_tests.cpp)
  target_link_libraries(file_save_tests PRIVATE youngdraw_storage)
  add_test(NAME atomic_file_save COMMAND file_save_tests)
  add_executable(project_tests tests/project_tests.cpp)
  target_link_libraries(project_tests PRIVATE youngdraw_editor)
  add_test(NAME project_management COMMAND project_tests)
  add_executable(project_manager_ui_tests tests/project_manager_ui_tests.cpp)
  target_link_libraries(project_manager_ui_tests PRIVATE youngdraw_editor)
  add_test(NAME project_manager_ui COMMAND project_manager_ui_tests)
  set_tests_properties(project_manager_ui PROPERTIES TIMEOUT 30)
  add_test(NAME office_roundtrip COMMAND YoungDraw --test-office)
  add_executable(grid_tests tests/grid_tests.cpp)
  target_link_libraries(grid_tests PRIVATE youngdraw_editor)
  add_test(NAME grid_regression COMMAND grid_tests)
  add_executable(document_tabs_tests tests/document_tabs_tests.cpp)
  target_link_libraries(document_tabs_tests PRIVATE youngdraw_editor)
  add_test(NAME document_tabs COMMAND document_tabs_tests)
  add_test(NAME graph_energy_units COMMAND document_tabs_tests --plot-units-only)
  add_executable(theme_tests tests/theme_tests.cpp resources/youngdraw.rc)
  target_include_directories(theme_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  target_link_libraries(theme_tests PRIVATE youngdraw_editor)
  add_test(NAME appearance_themes COMMAND theme_tests)
  add_executable(gif_tests tests/gif_tests.cpp)
  target_link_libraries(gif_tests PRIVATE youngdraw_editor)
  configure_file(tests/fixtures/animated.gif animated.gif COPYONLY)
  configure_file(tests/fixtures/transparent-disposal.gif transparent-disposal.gif COPYONLY)
  add_test(NAME animated_gif COMMAND gif_tests)
  add_executable(mechanism_tests tests/mechanism_tests.cpp)
  target_link_libraries(mechanism_tests PRIVATE youngdraw_cdxml)
  add_test(NAME mechanism_regression COMMAND mechanism_tests)
  add_executable(mechanism_edit_tests tests/mechanism_edit_tests.cpp)
  target_link_libraries(mechanism_edit_tests PRIVATE youngdraw_cdxml)
  add_test(NAME mechanism_editing COMMAND mechanism_edit_tests)
  add_executable(mechanism_graph_tests tests/mechanism_graph_tests.cpp)
  target_link_libraries(mechanism_graph_tests PRIVATE youngdraw_cdxml)
  add_test(NAME mechanism_graph_editing COMMAND mechanism_graph_tests)
  add_executable(cdx_tests tests/cdx_tests.cpp)
  target_link_libraries(cdx_tests PRIVATE youngdraw_cdxml)
  add_test(NAME cdx_import COMMAND cdx_tests)
endif()

add_executable(plot_layout_tests tests/plot_layout_tests.cpp)
target_link_libraries(plot_layout_tests PRIVATE youngdraw_core)
if(WIN32)
  target_link_libraries(plot_layout_tests PRIVATE youngdraw_cdxml)
  target_compile_definitions(plot_layout_tests PRIVATE TEST_CDXML)
endif()
add_test(NAME plot_layout COMMAND plot_layout_tests)
