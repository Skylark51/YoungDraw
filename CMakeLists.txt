cmake_minimum_required(VERSION 3.20)
project(YoungDraw VERSION 0.4.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_library(youngdraw_core src/model.cpp src/ligand_spatial.cpp src/structure_templates.cpp src/layout.cpp src/layout_refine.cpp src/plot_data.cpp src/mechanism_data.cpp src/mechanism_graph.cpp src/mechanism_edit.cpp src/scene.cpp src/arrows.cpp)
target_include_directories(youngdraw_core PUBLIC src)
if(WIN32)
  add_library(youngdraw_storage src/win_storage.cpp src/win_projects.cpp src/win_recovery.cpp)
  target_compile_definitions(youngdraw_storage PUBLIC UNICODE _UNICODE NOMINMAX)
  target_include_directories(youngdraw_storage PUBLIC src)
  add_library(youngdraw_cdxml src/cdxml.cpp src/cdx.cpp src/ligand_templates.cpp)
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
    src/win_mechanism_editor.cpp
    src/win_editor_plot.cpp
    src/win_plot_manager.cpp
    src/win_editor_structure.cpp
    src/win_editor_commands.cpp
    src/win_editor_canvas.cpp
    src/win_editor_pointer.cpp
    src/win_editor_shortcuts.cpp
    src/win_editor_window.cpp
    src/win_project_manager.cpp
    src/win_splash.cpp
    src/win_button_help.cpp
    src/win_dialogs.cpp
    src/win_color_picker.cpp
    src/win_grid.cpp
    src/win_gallery.cpp
    src/office.cpp
    src/win_theme.cpp)
  set_property(SOURCE src/win_editor_state.cpp APPEND PROPERTY COMPILE_DEFINITIONS "YOUNGDRAW_VERSION=L\"${PROJECT_VERSION}\"")
  target_link_libraries(youngdraw_editor PUBLIC youngdraw_storage youngdraw_cdxml
    gdiplus comdlg32 shell32 comctl32 oleaut32 uuid advapi32 dwmapi uxtheme)
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
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME ux_auditor_unit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_ux_auditor.py")
add_test(NAME ui_evidence_comparison_unit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_compare_ui_evidence.py")
set_tests_properties(ux_auditor_unit ui_evidence_comparison_unit PROPERTIES TIMEOUT 30)
if(WIN32)
  add_executable(workflow_audit_tests tests/workflow_audit_tests.cpp)
  target_link_libraries(workflow_audit_tests PRIVATE youngdraw_editor)
  add_test(NAME workflow_access_audit COMMAND workflow_audit_tests)
  set_tests_properties(workflow_access_audit PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_executable(ligand_spatial_tests tests/ligand_spatial_tests.cpp)
  target_link_libraries(ligand_spatial_tests PRIVATE youngdraw_editor)
  add_test(NAME ligand_spatial COMMAND ligand_spatial_tests)
  add_test(NAME ligand_template_export COMMAND YoungDraw --export-ligand-templates "${CMAKE_CURRENT_BINARY_DIR}/ligand-previews")
  set_tests_properties(ligand_spatial PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
  add_executable(quality_tests tests/quality_tests.cpp)
  target_link_libraries(quality_tests PRIVATE youngdraw_storage youngdraw_cdxml)
  add_test(NAME quality_regression COMMAND quality_tests)
  add_executable(pointer_quality_tests tests/pointer_quality_tests.cpp)
  target_link_libraries(pointer_quality_tests PRIVATE youngdraw_editor)
  add_test(NAME pointer_quality COMMAND pointer_quality_tests)
  set_tests_properties(quality_regression pointer_quality PROPERTIES TIMEOUT 90)
  add_executable(ui_access_validation tests/ui_access_validation.cpp resources/youngdraw.rc)
  target_include_directories(ui_access_validation PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  target_link_libraries(ui_access_validation PRIVATE youngdraw_editor)
  add_test(NAME ui_access_validation COMMAND ui_access_validation)
  set_tests_properties(ui_access_validation PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
  add_test(NAME ux_runtime_audit COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ux-auditor.py"
    --evidence-dir "${CMAKE_CURRENT_BINARY_DIR}/ui-access-validation"
    --output-dir "${CMAKE_CURRENT_BINARY_DIR}/ux-audit"
    --graph "${CMAKE_CURRENT_SOURCE_DIR}/docs/december-ux/graph-after.json"
    --workflows "${CMAKE_CURRENT_SOURCE_DIR}/docs/december-ux/workflows.json"
    --constraints "${CMAKE_CURRENT_SOURCE_DIR}/docs/december-ux/constraints.json"
    --fail-on-constraints)
  set_tests_properties(ui_access_validation PROPERTIES FIXTURES_SETUP ux_runtime_evidence)
  set_tests_properties(ux_runtime_audit PROPERTIES FIXTURES_REQUIRED ux_runtime_evidence TIMEOUT 30)
endif()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE youngdraw_core)
if(WIN32)
  target_link_libraries(core_tests PRIVATE youngdraw_cdxml)
  target_compile_definitions(core_tests PRIVATE TEST_CDXML)
endif()
add_test(NAME core_tests COMMAND core_tests)
add_executable(layout_refine_tests tests/layout_refine_tests.cpp)
target_link_libraries(layout_refine_tests PRIVATE youngdraw_core)
add_test(NAME layout_refinement COMMAND layout_refine_tests)
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
  add_test(NAME label_bond_alignment COMMAND document_tabs_tests --label-alignment-only)
  set_tests_properties(label_bond_alignment PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_test(NAME atom_radicals COMMAND document_tabs_tests --atom-radicals-only)
  set_tests_properties(atom_radicals PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  set_tests_properties(document_tabs PROPERTIES TIMEOUT 90)
  add_executable(document_shortcuts_tests tests/document_shortcuts_tests.cpp)
  target_link_libraries(document_shortcuts_tests PRIVATE youngdraw_editor)
  add_test(NAME document_shortcuts COMMAND document_shortcuts_tests)
  set_tests_properties(document_shortcuts PROPERTIES TIMEOUT 30)
  add_test(NAME graph_energy_units COMMAND document_tabs_tests --plot-units-only)
  add_test(NAME graph_manager COMMAND document_tabs_tests --plot-manager-only)
  add_test(NAME plot_axes_manager COMMAND document_tabs_tests --unified-axes-only)
  set_tests_properties(plot_axes_manager PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_test(NAME axis_quick_access COMMAND document_tabs_tests --axis-quick-access-only)
  set_tests_properties(axis_quick_access PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  set_tests_properties(graph_manager PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_test(NAME plot_annotation_preview COMMAND document_tabs_tests --annotation-preview-only)
  add_test(NAME energy_gap_style COMMAND document_tabs_tests --energy-gap-style-only)
  set_tests_properties(energy_gap_style PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  set_tests_properties(plot_annotation_preview PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_test(NAME png_export COMMAND document_tabs_tests --png-export-only)
  set_tests_properties(png_export PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_executable(theme_tests tests/theme_tests.cpp resources/youngdraw.rc)
  target_include_directories(theme_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  target_link_libraries(theme_tests PRIVATE youngdraw_editor)
  add_test(NAME appearance_themes COMMAND theme_tests)
  add_executable(editor_polish_tests tests/editor_polish_tests.cpp)
  target_link_libraries(editor_polish_tests PRIVATE youngdraw_editor)
  add_test(NAME editor_tools_custom_theme COMMAND editor_polish_tests)
  set_tests_properties(editor_tools_custom_theme PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  set_tests_properties(appearance_themes PROPERTIES TIMEOUT 60)
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
  add_executable(mechanism_editor_ui_tests tests/mechanism_editor_ui_tests.cpp)
  target_link_libraries(mechanism_editor_ui_tests PRIVATE youngdraw_editor)
  add_test(NAME mechanism_editor_ui COMMAND mechanism_editor_ui_tests)
  set_tests_properties(mechanism_editor_ui PROPERTIES TIMEOUT 30)
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

if(WIN32)
  add_executable(november_validation tests/november_validation.cpp)
  target_link_libraries(november_validation PRIVATE youngdraw_cdxml)
  add_test(NAME november_complex_graphs COMMAND november_validation)
endif()

if(WIN32)
  add_executable(office_storage_tests tests/office_storage_tests.cpp)
  target_link_libraries(office_storage_tests PRIVATE youngdraw_editor)
  add_test(NAME office_storage_roundtrip COMMAND office_storage_tests)
endif()

add_executable(reaction_profile_tests tests/reaction_profile_tests.cpp)
target_link_libraries(reaction_profile_tests PRIVATE youngdraw_core)
if(WIN32)
  target_link_libraries(reaction_profile_tests PRIVATE youngdraw_cdxml)
  target_compile_definitions(reaction_profile_tests PRIVATE TEST_CDXML)
endif()
add_test(NAME reaction_profile_annotations COMMAND reaction_profile_tests)
