cmake_minimum_required(VERSION 3.16)
project(open_rikky_module VERSION 1.4 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "open-rikky_module targets 32-bit x86 AviUtl / ExEdit (configure with -A Win32).")
endif()

if(MSVC)
	add_compile_options(/W3 /O2 /wd4819 /wd5105 /D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(rikky_memory SHARED
	src/memory/rikky_memory.c
	src/memory/rikky_memory.def
)

target_include_directories(rikky_memory PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(rikky_memory PROPERTIES
	PREFIX ""
	OUTPUT_NAME "rikky_memory"
	SUFFIX ".auf"
)

target_link_libraries(rikky_memory PRIVATE
	user32
	kernel32
)

set(LUA51_IMPORT_LIB "${CMAKE_CURRENT_BINARY_DIR}/lua51.lib")

add_custom_command(
	OUTPUT "${LUA51_IMPORT_LIB}"
	COMMAND "${CMAKE_AR}" /NOLOGO "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/module/lua51.def" /MACHINE:X86 "/OUT:${LUA51_IMPORT_LIB}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/module/lua51.def"
	COMMENT "Generating lua51.lib import library"
)

add_custom_target(lua51_imp DEPENDS "${LUA51_IMPORT_LIB}")

add_library(rikky_module SHARED
	src/module/module_main.c
	src/module/info_sys.c
	src/module/data_text.c
	src/module/color_math.c
	src/module/dialog_ui.c
	src/module/image_io.c
	src/module/pixel_fx.c
	src/module/exedit_obj.c
	src/module/camera_draw3d.c
	src/module/audio_midi.c
	src/module/rikky_module.def
)

add_dependencies(rikky_module lua51_imp)

target_include_directories(rikky_module PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(rikky_module PROPERTIES
	PREFIX ""
	OUTPUT_NAME "rikky_module"
	SUFFIX ".dll"
)

target_link_libraries(rikky_module PRIVATE
	rikky_memory
	"${LUA51_IMPORT_LIB}"
	user32
	gdi32
	gdiplus
	comdlg32
	shell32
	ole32
	winmm
	kernel32
)

