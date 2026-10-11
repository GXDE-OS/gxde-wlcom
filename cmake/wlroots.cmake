include_guard(GLOBAL)
include(ExternalProject)

if(NOT DEFINED WLCOM_ROOT_DIR)
  get_filename_component(WLCOM_ROOT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
  )
endif()

find_program(MESON_EXECUTABLE meson REQUIRED)
find_package(PkgConfig REQUIRED)

set(WLCOM_WLROOTS_RENDERERS "gles2,vulkan" CACHE STRING
  "Comma separated wlroots renderers (gles2, vulkan)"
)
option(WLCOM_XWAYLAND "Build wlroots and gxde-wlcom with Xwayland support" ON)

string(REPLACE "," ";" _wlroots_renderers "${WLCOM_WLROOTS_RENDERERS}")
# src/render/renderer.c includes <wlr/render/vulkan.h> unconditionally.
if(NOT "vulkan" IN_LIST _wlroots_renderers)
  message(FATAL_ERROR
    "WLCOM_WLROOTS_RENDERERS must contain vulkan, got "
    "'${WLCOM_WLROOTS_RENDERERS}'"
  )
endif()

# Everything wlroots needs comes from the operating system; every module listed
# here ends up in the Requires of the generated wlroots.pc below, so the static
# archive is linked against exactly what Meson enabled.
set(_wlroots_requires
  "wayland-server>=1.22"
  wayland-client
  "libdrm>=2.4.120"
  xkbcommon
  "pixman-1>=0.42.0"
  # session
  libudev
  "libseat>=0.2.0"
  # DRM backend
  libdisplay-info
  # libinput backend
  "libinput>=1.14.0"
  # gbm allocator
  "gbm>=17.1.0"
)
set(_wlroots_meson_options
  "-Dallocators=gbm"
  "-Dsession=enabled"
  "-Drenderers=${WLCOM_WLROOTS_RENDERERS}"
)

if("gles2" IN_LIST _wlroots_renderers)
  list(APPEND _wlroots_requires egl glesv2)
endif()
if("vulkan" IN_LIST _wlroots_renderers)
  list(APPEND _wlroots_requires "vulkan>=1.2.182")
  find_program(GLSLANG_EXECUTABLE NAMES glslang glslangValidator REQUIRED)
endif()

if(WLCOM_XWAYLAND)
  list(APPEND _wlroots_requires
    xcb
    xcb-composite
    xcb-ewmh
    xcb-icccm
    xcb-render
    xcb-res
    xcb-xfixes
  )
  list(APPEND _wlroots_meson_options "-Dxwayland=enabled")
else()
  list(APPEND _wlroots_meson_options "-Dxwayland=disabled")
endif()

pkg_check_modules(WLROOTS_REQUIRED_SYSTEM_DEPS REQUIRED ${_wlroots_requires})
if(WLCOM_XWAYLAND)
  # xwayland.pc only provides the Xwayland path, nothing to link against.
  pkg_check_modules(WLCOM_SYSTEM_XWAYLAND REQUIRED xwayland)
endif()
pkg_check_modules(WLCOM_SYSTEM_HWDATA REQUIRED hwdata)
pkg_check_modules(WLCOM_SYSTEM_WAYLAND_SCANNER REQUIRED wayland-scanner)
pkg_check_modules(WLCOM_SYSTEM_WAYLAND_PROTOCOLS REQUIRED
  "wayland-protocols>=1.32"
)

# Optional wlroots dependencies. Meson would auto-detect them on its own; make
# the decision here instead so the generated pkg-config file always matches.
if(WLCOM_XWAYLAND)
  pkg_check_modules(WLCOM_SYSTEM_XCB_ERRORS QUIET xcb-errors)
  if(WLCOM_SYSTEM_XCB_ERRORS_FOUND)
    list(APPEND _wlroots_requires xcb-errors)
    list(APPEND _wlroots_meson_options "-Dxcb-errors=enabled")
  else()
    list(APPEND _wlroots_meson_options "-Dxcb-errors=disabled")
  endif()
endif()

# gxde-wlcom needs the DRM and libinput backends; the X11 backend (nested
# runs inside an X session) is built whenever its libraries are available.
set(_wlroots_x11_requires
  xcb
  xcb-dri3
  xcb-present
  xcb-render
  xcb-renderutil
  xcb-shm
  xcb-xfixes
  xcb-xinput
)
pkg_check_modules(WLCOM_SYSTEM_X11_BACKEND QUIET ${_wlroots_x11_requires})
if(WLCOM_SYSTEM_X11_BACKEND_FOUND)
  foreach(_module IN LISTS _wlroots_x11_requires)
    if(NOT _module IN_LIST _wlroots_requires)
      list(APPEND _wlroots_requires ${_module})
    endif()
  endforeach()
  list(APPEND _wlroots_meson_options "-Dbackends=drm,libinput,x11")
else()
  list(APPEND _wlroots_meson_options "-Dbackends=drm,libinput")
endif()

# libliftoff has no Meson option: wlroots uses it whenever pkg-config finds it,
# and --wrap-mode=nodownload stops it from falling back to a subproject.
pkg_check_modules(WLCOM_SYSTEM_LIBLIFTOFF QUIET "libliftoff>=0.4.0")
if(WLCOM_SYSTEM_LIBLIFTOFF_FOUND)
  list(APPEND _wlroots_requires "libliftoff>=0.4.0")
endif()

# Follow the CMake build type so a Debug gxde-wlcom also gets a debug wlroots.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _wlroots_build_type)
if(_wlroots_build_type STREQUAL "DEBUG")
  set(_wlroots_meson_buildtype debug)
elseif(_wlroots_build_type STREQUAL "RELEASE")
  set(_wlroots_meson_buildtype release)
elseif(_wlroots_build_type STREQUAL "MINSIZEREL")
  set(_wlroots_meson_buildtype minsize)
elseif(_wlroots_build_type STREQUAL "NONE")
  # Distribution builds (Arch's makepkg) pass their own CFLAGS.
  set(_wlroots_meson_buildtype plain)
else()
  set(_wlroots_meson_buildtype debugoptimized)
endif()

set(WLROOTS_VERSION 0.17.4)
set(WLROOTS_VERSION_MAJOR 0)
set(WLROOTS_VERSION_MINOR 17)
set(WLROOTS_VERSION_PATCH 4)

set(WLROOTS_SOURCE_DIR "${WLCOM_ROOT_DIR}/libs/wlroots")
set(WLROOTS_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/wlroots-build")
set(WLROOTS_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/wlroots-install")
set(WLROOTS_LIBRARY "${WLROOTS_INSTALL_DIR}/lib/libwlroots.a")
set(WLROOTS_PKGCONFIG_DIR "${CMAKE_BINARY_DIR}/_deps/wlroots-pkgconfig")

ExternalProject_Add(wlroots_external
  SOURCE_DIR "${WLROOTS_SOURCE_DIR}"
  BINARY_DIR "${WLROOTS_BUILD_DIR}"
  CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -E env
    "CC=${CMAKE_C_COMPILER}"
    "${MESON_EXECUTABLE}" setup
    "<BINARY_DIR>"
    "<SOURCE_DIR>"
    "--prefix=${WLROOTS_INSTALL_DIR}"
    "--libdir=lib"
    "--buildtype=${_wlroots_meson_buildtype}"
    "--wrap-mode=nodownload"
    "-Dexamples=false"
    "-Dwerror=false"
    "-Ddefault_library=static"
    ${_wlroots_meson_options}
  BUILD_COMMAND "${MESON_EXECUTABLE}" compile -C "<BINARY_DIR>"
  INSTALL_COMMAND "${MESON_EXECUTABLE}" install -C "<BINARY_DIR>"
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS "${WLROOTS_LIBRARY}"
)

file(MAKE_DIRECTORY
  "${WLROOTS_INSTALL_DIR}/include"
  "${WLROOTS_INSTALL_DIR}/lib"
  "${WLROOTS_PKGCONFIG_DIR}"
)

if(WLCOM_XWAYLAND)
  set(WLROOTS_HAVE_XWAYLAND true)
else()
  set(WLROOTS_HAVE_XWAYLAND false)
endif()

# Expose the Meson-built static archive to CMake through the same pkg-config
# contract that upstream wlroots installs. The archive and generated headers
# are produced by wlroots_external before consumers are compiled.
list(JOIN _wlroots_requires ", " _wlroots_requires_line)
# pkg_check_modules() wants "foo>=1", a .pc file wants "foo >= 1".
string(REGEX REPLACE "([<>=]+)" " \\1 " _wlroots_requires_line
  "${_wlroots_requires_line}"
)
file(WRITE "${WLROOTS_PKGCONFIG_DIR}/wlroots.pc"
"prefix=${WLROOTS_INSTALL_DIR}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: wlroots
Description: GXDE wlcom vendored wlroots build
Version: ${WLROOTS_VERSION}
Requires: ${_wlroots_requires_line}
Cflags: -I\${includedir} -DWLR_USE_UNSTABLE
Libs: -L\${libdir} -lwlroots -lm -lrt

have_xwayland=${WLROOTS_HAVE_XWAYLAND}
")

set(_wlcom_saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
if(_wlcom_saved_pkg_config_path)
  set(ENV{PKG_CONFIG_PATH}
    "${WLROOTS_PKGCONFIG_DIR}:${_wlcom_saved_pkg_config_path}"
  )
else()
  set(ENV{PKG_CONFIG_PATH} "${WLROOTS_PKGCONFIG_DIR}")
endif()

# pkg_check_modules() skips the lookup while its arguments stay the same, but
# wlroots.pc above changes with the options; always read it again.
unset(__pkg_config_checked_WLCOM_WLROOTS CACHE)
pkg_check_modules(WLCOM_WLROOTS REQUIRED IMPORTED_TARGET GLOBAL
  "wlroots=${WLROOTS_VERSION}"
)

set(ENV{PKG_CONFIG_PATH} "${_wlcom_saved_pkg_config_path}")

add_dependencies(PkgConfig::WLCOM_WLROOTS wlroots_external)
# libwlroots.a does not exist yet at configure time, so pkg-config's -lwlroots
# cannot be resolved to a full path; let the linker look it up instead.
target_link_directories(PkgConfig::WLCOM_WLROOTS INTERFACE
  "${WLROOTS_INSTALL_DIR}/lib"
)

add_library(Wlroots::wlroots ALIAS PkgConfig::WLCOM_WLROOTS)
