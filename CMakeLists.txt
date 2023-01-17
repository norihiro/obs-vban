cmake_minimum_required(VERSION 3.12)

project(obs-vban VERSION 0.2.3)

# Replace `Your Name Here` with the name (yours or your organization's) you want
# to see as the author of the plugin (in the plugin's metadata itself and in the installers)
set(PLUGIN_AUTHOR "Norihiro Kamae")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS releases
# (used both in the installer and when submitting the installer for notarization)
set(MACOS_BUNDLEID "net.nagater.obs-vban")
set(MACOS_PACKAGE_UUID "21123AEA-B27F-47AE-BAFA-857FF1A9AB87")
set(MACOS_INSTALLER_UUID "BF0251EB-CB6D-48C7-957F-BC20B79B9D42")
set(ID_PREFIX "net.nagater.obs-vban.")

# Replace `me@contoso.com` with the maintainer email address you want to put in Linux packages
set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

# TAKE NOTE: No need to edit things past this point

find_package(libobs REQUIRED)
include(cmake/ObsPluginHelpers.cmake)

configure_file(
	src/plugin-macros.h.in
	../src/plugin-macros.generated.h
)
configure_file(
	installer/installer-Windows.iss.in
	../installer/installer-Windows.generated.iss
)

configure_file(
	ci/ci_includes.sh.in
	../ci/ci_includes.generated.sh
)
configure_file(
	ci/ci_includes.cmd.in
	../ci/ci_includes.generated.cmd
)

set(PLUGIN_SOURCES
	src/plugin-main.c
	src/vban-source.c
	src/vban-udp-instance.c
	src/vban-udp-thread.c
	src/vban-output.c
	src/vban-output-thread.c
	src/vban-filter.c
)

add_library(${CMAKE_PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

target_link_libraries(${CMAKE_PROJECT_NAME}
	OBS::libobs
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE vban)

if(OS_WINDOWS)
	# Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL when building with VS2019)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
		add_definitions("-D_CRT_SECURE_NO_WARNINGS") # to avoid a warning for `strncpy`
	endif()

	target_link_libraries(${CMAKE_PROJECT_NAME} OBS::w32-pthreads)
	target_link_libraries(${CMAKE_PROJECT_NAME} wsock32 ws2_32)
endif()

if(OS_LINUX)
	target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
endif()

setup_plugin_target(${CMAKE_PROJECT_NAME})

configure_file(installer/installer-macOS.pkgproj.in installer-macOS.generated.pkgproj)
