
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 SelfTests")
set(CATCH_ENABLE_WERROR OFF CACHE BOOL "Disable Catch2 Werror")
find_package(Catch2 CONFIG)

if (${Catch2_FOUND})       
else ()
	set(CATCH2_VER "2.11.1")
	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/catch2.tar.gz")
		message(STATUS "Downloading Catch2")
		file(DOWNLOAD 
			"https://github.com/catchorg/Catch2/archive/v${CATCH2_VER}.tar.gz" 
			"${CMAKE_SOURCE_DIR}/thirdparty/catch2.tar.gz"
		)
	endif()

	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/Catch2-${CATCH2_VER}")
		message(STATUS "Decompress Catch2")
		execute_process(COMMAND 
			${CMAKE_COMMAND} -E tar xfz "${CMAKE_SOURCE_DIR}/thirdparty/catch2.tar.gz"
			WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/thirdparty"
		)
	endif()

	message(STATUS "Include Catch2")
	add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/Catch2-${CATCH2_VER})
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/thirdparty/Catch2-${CATCH2_VER}/contrib")
endif ()

include(Catch)