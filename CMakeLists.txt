cmake_minimum_required(VERSION 3.15)
project(content_based_image_retrieval LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the local Windows OpenCV install when available. Override with
# -DOpenCV_DIR=... if OpenCV is installed elsewhere.
if((NOT OpenCV_DIR OR OpenCV_DIR MATCHES "-NOTFOUND$")
		AND EXISTS "C:/opencv/build/OpenCVConfig.cmake")
	set(OpenCV_DIR "C:/opencv/build" CACHE PATH "Directory containing OpenCVConfig.cmake" FORCE)
endif()

# Executables go in out/, including with Visual Studio. Static libraries get a folder per
# configuration: Debug and Release share a filename, so with one folder the last build silently
# overwrote the other's .lib and Release links failed against a Debug library (LNK2019 on
# cv::debug_build_guard symbols).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/out")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/out/lib")
foreach(config ${CMAKE_CONFIGURATION_TYPES})
	string(TOUPPER "${config}" config_upper)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${PROJECT_SOURCE_DIR}/out")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${PROJECT_SOURCE_DIR}/out/lib/${config}")
endforeach()

find_package(OpenCV REQUIRED)

if(MSVC)
	add_compile_options(/W4)
else()
	add_compile_options(-Wall)
endif()

# Shared helpers used by the application executable(s).
add_library(cbir_common STATIC
	src/csv_util.cpp
	src/features.cpp
	src/common.cpp
	src/feature_store.cpp
	src/metrics.cpp
	src/sobel.cpp

	include/sobel.h
	include/features.h
	include/csv_util.h
	include/common.h
	include/feature_store.h
	include/metrics.h
	include/display.h
	src/display.cpp
)
target_include_directories(cbir_common PUBLIC
	"${PROJECT_SOURCE_DIR}/include"
	${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(cbir_common PUBLIC ${OpenCV_LIBS})

# For the standard Windows OpenCV package layout, copy runtime DLLs beside
# each executable so it can run without manually editing PATH.
if(WIN32)
	file(GLOB OPENCV_RUNTIME_DLLS
		"${OpenCV_DIR}/x64/*/bin/*.dll"
		"${OpenCV_DIR}/bin/*.dll"
	)
endif()

function(add_cbir_executable target)
	add_executable(${target} ${ARGN})
	target_link_libraries(${target} PRIVATE cbir_common)
	set_target_properties(${target} PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
	)

	if(WIN32 AND OPENCV_RUNTIME_DLLS)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				${OPENCV_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${target}>"
			COMMAND_EXPAND_LISTS
			COMMENT "Copying OpenCV DLLs next to ${target}"
		)
	elseif(WIN32)
		message(WARNING "OpenCV DLLs were not found under OpenCV_DIR=${OpenCV_DIR}; add their bin folder to PATH or update the DLL search paths in CMakeLists.txt.")
	endif()
endfunction()

# Add each program here, listing its entry-point source file(s).
add_cbir_executable(extract src/extract.cpp)
add_cbir_executable(query src/query.cpp)
