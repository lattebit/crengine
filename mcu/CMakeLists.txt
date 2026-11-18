cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(crengine_mcu LANGUAGES C CXX)
    include(CTest)
endif()

# 中文：固件和主机共用固定源码；配置期间不下载依赖。
# English: Firmware and host share pinned sources; configuration never downloads dependencies.
set(CRE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(CRE_DEPS "${CMAKE_CURRENT_LIST_DIR}/deps")
foreach(dep freetype harfbuzz fribidi libunibreak utf8proc zlib xxhash md4c nanosvg)
    if(NOT EXISTS "${CRE_DEPS}/${dep}/.git")
        message(FATAL_ERROR "Initialize crengine dependencies: git submodule update --init --recursive")
    endif()
endforeach()
include(dependencies.cmake)

set(CRE_MCU_SOURCES
    cp_stats crtxtenc drawutil epubfmt hist hyphman lstridmap lvdocview
    lvdrawbuf lvfnt lvfntman lvimg lvmemman lvpagesplitter lvrend lvstream
    lvstring lvstsheet lvstyles lvtextfm lvtinydom lvxml mathml mdfmt
    props renderutil rtfimp textlang txtselector
)
list(TRANSFORM CRE_MCU_SOURCES PREPEND "${CRE_ROOT}/crengine/src/")
list(TRANSFORM CRE_MCU_SOURCES APPEND ".cpp")
configure_file(crsetup.h.in crsetup.h COPYONLY)
file(READ "${CRE_ROOT}/cr3gui/data/html5.css" CRE_HTML_CSS)
file(READ "${CRE_ROOT}/cr3gui/data/epub.css" CRE_EPUB_CSS)
file(READ "${CRE_ROOT}/cr3gui/data/fb2.css" CRE_FB2_CSS)
file(READ "${CRE_ROOT}/cr3gui/data/txt.css" CRE_TXT_CSS)
string(REPLACE "@import url(\"html5.css\");" "" CRE_EPUB_CSS "${CRE_EPUB_CSS}")
configure_file(cre_stylesheets.h.in cre_stylesheets.h @ONLY)
# 中文：缓存身份覆盖源码、裁剪配置、字体依赖和样式，无需初始化字体即可查询。
# English: Cache identity covers sources, trimming, font dependencies and styles without initializing fonts.
file(GLOB CRE_HEADERS CONFIGURE_DEPENDS "${CRE_ROOT}/crengine/include/*.h" "${CMAKE_CURRENT_LIST_DIR}/include/*.h")
file(GLOB_RECURSE CRE_PORTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/ports/*")
set(CRE_ID_INPUTS ${CRE_MCU_SOURCES} ${CRE_HEADERS} ${CRE_PORTS}
    "${CMAKE_CURRENT_LIST_FILE}" "${CMAKE_CURRENT_LIST_DIR}/dependencies.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp" "${CMAKE_CURRENT_LIST_DIR}/document.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crsetup.h.in" "${CMAKE_CURRENT_LIST_DIR}/cre_stylesheets.h.in")
set(CRE_ID_MATERIAL "${CRE_HTML_CSS}${CRE_EPUB_CSS}${CRE_FB2_CSS}${CRE_TXT_CSS}")
foreach(input IN LISTS CRE_ID_INPUTS)
    file(SHA256 "${input}" hash)
    string(APPEND CRE_ID_MATERIAL "${hash}")
endforeach()
foreach(dep freetype harfbuzz fribidi libunibreak utf8proc zlib xxhash md4c nanosvg)
    execute_process(COMMAND git -C "${CRE_DEPS}/${dep}" rev-parse HEAD OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    string(APPEND CRE_ID_MATERIAL "${revision}")
endforeach()
string(SHA256 CRE_BUILD_HASH "${CRE_ID_MATERIAL}")
string(SUBSTRING "${CRE_BUILD_HASH}" 0 16 CRE_BUILD_ID)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cre_identity.h" "#define CRE_BUILD_ID UINT64_C(0x${CRE_BUILD_ID})\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CRE_ID_INPUTS})
add_library(crengine_mcu STATIC ${CRE_MCU_SOURCES}
    "${CRE_ROOT}/crengine/qimagescale/qimagescale.cpp" runtime.cpp document.cpp)
add_library(crengine::mcu ALIAS crengine_mcu)
target_include_directories(crengine_mcu PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include" "${CRE_ROOT}/crengine/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(crengine_mcu PRIVATE "${CRE_DEPS}/nanosvg/src")
target_compile_features(crengine_mcu PUBLIC cxx_std_17)
target_compile_definitions(crengine_mcu PRIVATE DOC_BUFFER_SIZE=0x80000)
target_compile_options(crengine_mcu PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(crengine_mcu PUBLIC cre_freetype cre_harfbuzz cre_fribidi
    cre_unibreak cre_utf8proc cre_zlib cre_xxhash cre_md4c)
set_target_properties(crengine_mcu PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(cre_mcu_font_test tests/font_preview.cpp)
    target_link_libraries(cre_mcu_font_test PRIVATE crengine_mcu)
    target_compile_options(cre_mcu_font_test PRIVATE -UNDEBUG)
    set(CRE_TEST_FONT "" CACHE FILEPATH "Font file for real backend tests")
    add_executable(cre_mcu_layout_test tests/layout.cpp
        "${CRE_DEPS}/md4c/src/md4c-html.c" "${CRE_DEPS}/md4c/src/entity.c")
    target_link_libraries(cre_mcu_layout_test PRIVATE crengine_mcu)
    target_compile_options(cre_mcu_layout_test PRIVATE -UNDEBUG)
    add_executable(cre_mcu_document_test tests/document.cpp)
    target_link_libraries(cre_mcu_document_test PRIVATE crengine_mcu)
    target_compile_options(cre_mcu_document_test PRIVATE -UNDEBUG)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME mcu_fixtures COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/create_fixtures.py" "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set_tests_properties(mcu_fixtures PROPERTIES FIXTURES_SETUP document_fixtures)
    if(CRE_TEST_FONT)
        add_test(NAME mcu_layout COMMAND cre_mcu_layout_test "${CRE_TEST_FONT}" "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
        set_tests_properties(mcu_layout PROPERTIES FIXTURES_REQUIRED document_fixtures)
        add_test(NAME mcu_document COMMAND cre_mcu_document_test "${CRE_TEST_FONT}" "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
        set_tests_properties(mcu_document PROPERTIES FIXTURES_REQUIRED document_fixtures)
        add_test(NAME mcu_font_preview COMMAND cre_mcu_font_test "${CRE_TEST_FONT}")
    endif()
endif()
