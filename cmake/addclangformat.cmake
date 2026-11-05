# clang-format `format` / `format-check` targets over a pinned, vendored binary so
# every machine + CI formats identically (clang-format output drifts between
# versions — pinning is what makes format-check a meaningful gate). Mirrors the
# addXXX.cmake FetchContent pattern: the binary is cached under .cache/fetch/ like
# the libraries and placed in build/_deps (blow away build/ to clean it). The
# muttleyxd assets are bare executables, not archives, so DOWNLOAD_NO_EXTRACT.
#
# Neither target is part of ALL — invoke explicitly:
#   cmake --build build --target format         # rewrite sources in place
#   cmake --build build --target format-check   # dry-run, non-zero on diff (CI)
#
# The set of files is the single source of truth in .clang-format-include at the repo
# root. Override the binary with -DMULTI_CLANG_FORMAT=/path/to/clang-format; it has to
# be new enough to take --files.


# Each asset is held to its SHA-256: this runs at build time, and a tag alone does not stop an
# asset being replaced under it. The hash is also what makes .cache/fetch a cache, since CMake
# deletes and refetches a file it has no hash to check against. So the version is not a cache
# option, as another one would need its own hashes; -DMULTI_CLANG_FORMAT runs another binary.
set(MULTI_CLANG_FORMAT_VERSION "20")
set(_cf_tag "master-796e77c") # release tag (muttleyxd/clang-tools-static-binaries)

if(CMAKE_HOST_WIN32)
	set(_cf_asset "clang-format-${MULTI_CLANG_FORMAT_VERSION}_windows-amd64.exe")
	set(_cf_sha256 "44011742f30b2ebfd9013aa2b07d802b1b474186fb7904a2f773296f27ff15f9")
elseif(CMAKE_HOST_APPLE)
	if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		set(_cf_asset "clang-format-${MULTI_CLANG_FORMAT_VERSION}_macos-arm-arm64")
		set(_cf_sha256 "fe6b8450a8cf83de3f517e3b9a9b1bb925613e5fb59145d6d24ccca5fe17d442")
	else()
		set(_cf_asset "clang-format-${MULTI_CLANG_FORMAT_VERSION}_macosx-amd64")
		set(_cf_sha256 "f2ddeacd376c764fef49e7abf5621a36d632ed3e73a7d92a9e87fbb164b7b227")
	endif()
else()
	set(_cf_asset "clang-format-${MULTI_CLANG_FORMAT_VERSION}_linux-amd64")
	set(_cf_sha256 "e900c1e520b6c9b9c99e43c0f45ccd12927838741cfc60c077a33dec69bb60cc")
endif()

if(MULTI_CLANG_FORMAT)
	# Caller supplied their own binary — trust it.
	set(_cf_bin "${MULTI_CLANG_FORMAT}")
else()
	include(FetchContent)
	set(_cf_file "github.com/muttleyxd/clang-tools-static-binaries/releases/download/${_cf_tag}/${_cf_asset}")
	FetchContent_Declare(clangformat
		URL          "https://${_cf_file}"
		URL_HASH     "SHA256=${_cf_sha256}"
		DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${_cf_file}"
		DOWNLOAD_NO_EXTRACT TRUE
	)
	FetchContent_MakeAvailable(clangformat)
	set(_cf_bin "${clangformat_SOURCE_DIR}/${_cf_asset}")
	if(NOT CMAKE_HOST_WIN32)
		file(CHMOD "${_cf_bin}" PERMISSIONS
			OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
	endif()
endif()

# Resolve .clang-format-include -> the file list. Each non-comment line is either a
# folder (globbed recursively for C/C++ sources) or an explicit glob (contains '*').
file(STRINGS "${CMAKE_SOURCE_DIR}/.clang-format-include" _cf_lines)
set(_cf_files "")
foreach(_line IN LISTS _cf_lines)
	string(STRIP "${_line}" _line)
	if(_line STREQUAL "" OR _line MATCHES "^#")
		continue()
	endif()
	if(_line MATCHES "[*]")
		file(GLOB_RECURSE _cf_matched CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${_line}")
	else()
		file(GLOB_RECURSE _cf_matched CONFIGURE_DEPENDS
			"${CMAKE_SOURCE_DIR}/${_line}/*.h"
			"${CMAKE_SOURCE_DIR}/${_line}/*.inl"
			"${CMAKE_SOURCE_DIR}/${_line}/*.cpp")
	endif()
	list(APPEND _cf_files ${_cf_matched})
endforeach()
list(REMOVE_DUPLICATES _cf_files)

# The list reaches clang-format through a file, not the command line: Windows refuses a
# command of about 32,000 characters or more, which a few hundred absolute paths reach,
# and cmd reports it as "The system cannot execute the specified program". The globs
# above are CONFIGURE_DEPENDS, so adding or deleting a source rewrites this at build time.
set(_cf_list_file "${CMAKE_BINARY_DIR}/clang-format-files.txt")
list(JOIN _cf_files "\n" _cf_list)
file(WRITE "${_cf_list_file}" "${_cf_list}\n")

add_custom_target(format
	COMMAND "${_cf_bin}" -i --style=file "--files=${_cf_list_file}"
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Formatting sources (clang-format ${MULTI_CLANG_FORMAT_VERSION}, .clang-format-include)"
	VERBATIM)

add_custom_target(format-check
	COMMAND "${_cf_bin}" --dry-run --Werror --style=file "--files=${_cf_list_file}"
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Checking formatting (clang-format ${MULTI_CLANG_FORMAT_VERSION}, .clang-format-include)"
	VERBATIM)
