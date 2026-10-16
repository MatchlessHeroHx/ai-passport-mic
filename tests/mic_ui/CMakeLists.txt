cmake_minimum_required(VERSION 3.16)
project(mic_ui_test C CXX)
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" lvgl)
# Embed the clip pack under the asm labels mic_ui.c uses (ESP-IDF EMBED_FILES names).
enable_language(ASM)
set(MIC_CLIPS_BIN "${REPO_ROOT}/assets/images/mic_clips.bin")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mic_clips_blob.S"
"    .section __DATA,__const\n    .p2align 2\n    .globl _binary_mic_clips_bin_start\n_binary_mic_clips_bin_start:\n    .incbin \"${MIC_CLIPS_BIN}\"\n    .globl _binary_mic_clips_bin_end\n_binary_mic_clips_bin_end:\n")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/mic_clips_blob.S" PROPERTIES OBJECT_DEPENDS "${MIC_CLIPS_BIN}")
add_executable(render_mic render.c
    "${REPO_ROOT}/main/mic_ui.c"
    "${REPO_ROOT}/assets/fonts/mic_font_16.c"
    "${REPO_ROOT}/assets/fonts/mic_font_20.c"
    "${REPO_ROOT}/main/mic_clips.c"
    "${REPO_ROOT}/main/mic_lines.c"
    "${CMAKE_CURRENT_BINARY_DIR}/mic_clips_blob.S"
    "${REPO_ROOT}/components/bsp/src/bsp_display_rounding.c")
target_include_directories(render_mic PRIVATE
    "${REPO_ROOT}/main" "${REPO_ROOT}/components/bsp/src")
target_link_libraries(render_mic PRIVATE lvgl::lvgl)
target_compile_options(render_mic PRIVATE -UNDEBUG)
