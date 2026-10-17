cmake_minimum_required(VERSION 3.28)
project(AirShow LANGUAGES CXX C)  # C required for UxPlay lib/ C sources

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt6 — must come before GStreamer to add Qt6's FindGStreamer.cmake to the module path
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml Network)
qt_standard_project_setup()

# GStreamer via pkg-config (per D-01)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.20
  gstreamer-video-1.0>=1.20
  gstreamer-app-1.0>=1.20
  gstreamer-audio-1.0>=1.20
)

# Avahi (mDNS advertisement on Linux, D-01)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET
    avahi-client
  )
endif()

# libupnp (pupnp) — DLNA SSDP advertisement (DISC-03).
# Use the system package when present; otherwise (e.g. MSYS2, which has no
# libupnp package) build pupnp from source as a static library.
pkg_check_modules(UPNP IMPORTED_TARGET libupnp)
if(UPNP_FOUND)
  # Older 1.14.x releases (e.g. Ubuntu 22.04's 1.14.12) lack UpnpActionRequest.h.
  find_file(AIRSHOW_UPNP_ACTION_REQUEST_H UpnpActionRequest.h
            PATHS ${UPNP_INCLUDE_DIRS} NO_DEFAULT_PATH NO_CACHE)
  if(NOT AIRSHOW_UPNP_ACTION_REQUEST_H)
    message(STATUS "System libupnp ${UPNP_VERSION} is too old (no UpnpActionRequest.h)")
    set(UPNP_FOUND FALSE)
  endif()
endif()
if(UPNP_FOUND)
  set(AIRSHOW_UPNP_TARGET PkgConfig::UPNP)
else()
  message(STATUS "Building pupnp from source")
  include(FetchContent)
  set(UPNP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(UPNP_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(UPNP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(UPNP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(UPNP_ENABLE_TESTING_INTEGRATION OFF CACHE BOOL "" FORCE)
  set(UPNP_ENABLE_BACKTRACE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(pupnp
    URL https://github.com/pupnp/pupnp/archive/refs/tags/release-22.1.8.tar.gz
    URL_HASH SHA256=abc191bd8f083c9ae73c93d9fa6ba051d54e086430da426c97ff7d989d581fec)
  FetchContent_MakeAvailable(pupnp)
  # Consumers of the static lib must not see dllimport declarations on Windows.
  target_compile_definitions(upnp_static INTERFACE UPNP_STATIC_LIB)
  set(AIRSHOW_UPNP_TARGET UPNP::Static)
endif()

# spdlog — optional, fall back to GLib logging if absent
find_package(spdlog QUIET)

# OpenSSL — link now to avoid header-change task in future phases (per RESEARCH.md Standard Stack)
find_package(OpenSSL REQUIRED)

# Protobuf — prefer protobuf's own CMake package: protobuf >= 22 depends on Abseil,
# which only the CONFIG package links. Fall back to CMake's FindProtobuf for older
# distro packages (e.g. Ubuntu's 3.21) that ship no CMake config.
# Headers are vendored to /tmp/protobuf-dev when libprotobuf-dev is not system-installed.
list(APPEND CMAKE_PREFIX_PATH "/tmp/protobuf-dev/usr")
set(protobuf_MODULE_COMPATIBLE ON)  # provides protobuf_generate_cpp in CONFIG mode
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()
message(STATUS "Protobuf version: ${Protobuf_VERSION}")
message(STATUS "Protobuf include dirs: ${Protobuf_INCLUDE_DIRS}")

# CASTV2 protobuf code generation
# Generated files land in ${CMAKE_CURRENT_BINARY_DIR}; added to airshow and test_cast targets.
protobuf_generate_cpp(CAST_PROTO_SRCS CAST_PROTO_HDRS src/cast/cast_channel.proto)
# Make available to tests/CMakeLists.txt via cache
set(CAST_PROTO_SRCS "${CAST_PROTO_SRCS}" CACHE INTERNAL "CASTV2 protobuf generated sources")
set(CAST_PROTO_HDRS "${CAST_PROTO_HDRS}" CACHE INTERNAL "CASTV2 protobuf generated headers")

# libplist — required by UxPlay lib/ for Apple plist parsing (D-07)
pkg_check_modules(PLIST REQUIRED IMPORTED_TARGET libplist-2.0)

# UxPlay lib/ — builds the 'airplay' static library target (D-01)
# Include lib/ directly to skip uxplay's renderers/ and uxplay.cpp (D-03)
# Must add llhttp and playfair subdirs first — airplay target links against them
# (mirrors the order in vendor/uxplay/CMakeLists.txt: llhttp, playfair, lib)
# Pre-set OPENSSL_FOUND so UxPlay's find_package(OpenSSL 1.1.1) uses already-found OpenSSL
set(OPENSSL_FOUND TRUE)
# Windows: UxPlay's dnssd.c loads dnssd.dll at runtime, so only dns_sd.h is needed
# to build — use the vendored copy instead of requiring the Bonjour SDK.
if(WIN32)
  set(DNSSD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/avahi/include/avahi-compat-libdns_sd"
      CACHE PATH "Directory containing dns_sd.h")
endif()
add_subdirectory(vendor/uxplay/lib/llhttp)
add_subdirectory(vendor/uxplay/lib/playfair)
add_subdirectory(vendor/uxplay/lib)
if(WIN32)
  # ...and drop the Bonjour SDK import library UxPlay links unconditionally.
  foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_airplay_libs airplay ${prop})
    if(_airplay_libs)
      list(FILTER _airplay_libs EXCLUDE REGEX "dnssd\\.lib$")
      set_target_properties(airplay PROPERTIES ${prop} "${_airplay_libs}")
    endif()
  endforeach()
endif()

# Main executable
qt_add_executable(airshow
  src/main.cpp
  src/pipeline/MediaPipeline.cpp
  src/ui/ReceiverWindow.cpp
  src/ui/AudioBridge.cpp
  src/ui/ConnectionBridge.cpp
  src/ui/SettingsBridge.cpp
  src/ui/VideoFrameSink.cpp
  src/protocol/ProtocolManager.cpp
  src/protocol/AirPlayHandler.cpp
  src/protocol/DlnaHandler.cpp
  src/protocol/CastHandler.cpp
  src/protocol/CastSession.cpp
  src/cast/CastStreamingOffer.cpp
  src/cast/CastStreamingReceiver.cpp
  src/protocol/MiracastHandler.cpp
  src/protocol/AirShowHandler.cpp
  ${CAST_PROTO_SRCS}
  src/settings/AppSettings.cpp
  src/security/SecurityManager.cpp
  src/discovery/ServiceAdvertiser.cpp
  src/discovery/AvahiAdvertiser.cpp
  src/discovery/BonjourAdvertiser.cpp
  src/discovery/DiscoveryManager.cpp
  src/discovery/UpnpAdvertiser.cpp
  src/web/WebInterface.cpp
  src/platform/WindowsFirewall.cpp
)

# QML lives under Qt's standard :/qt/qml/ resource prefix (on the default import
# path, so the binary doesn't depend on a qmldir next to it), and the module's
# build output goes to qml/AirShow — a top-level AirShow/ dir would collide with
# the airshow executable on case-insensitive filesystems (macOS, Windows).
qt_policy(SET QTP0001 NEW)
qt_add_qml_module(airshow
  URI AirShow
  VERSION 1.0
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/AirShow
  QML_FILES
    qml/main.qml
    qml/HudOverlay.qml
    qml/IdleScreen.qml
    qml/ApprovalDialog.qml
)

target_include_directories(airshow PRIVATE
  src
  src/ui   # Required for QML_ELEMENT auto-generated type registrations to find VideoFrameSink.h
  vendor/uxplay/lib
  ${CMAKE_CURRENT_BINARY_DIR}
  ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(airshow PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
  Qt6::Network
  PkgConfig::GST
  ${AIRSHOW_UPNP_TARGET}
  OpenSSL::SSL
  OpenSSL::Crypto
  airplay
  PkgConfig::PLIST
  protobuf::libprotobuf
)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(airshow PRIVATE PkgConfig::AVAHI)
endif()

# Windows: iphlpapi for MAC address, ws2_32 for sockets.
# dnssd.dll is loaded at runtime (no Bonjour SDK needed to build).
if(WIN32)
  target_link_libraries(airshow PRIVATE iphlpapi ws2_32)
endif()

# macOS: dns_sd is part of the system framework
if(APPLE)
  target_link_libraries(airshow PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
endif()

if(spdlog_FOUND)
  target_link_libraries(airshow PRIVATE spdlog::spdlog)
  target_compile_definitions(airshow PRIVATE HAVE_SPDLOG)
endif()

# Copy DLNA device description XML to build directory so UpnpAdvertiser can find it at runtime
configure_file(resources/dlna/MediaRenderer.xml
               ${CMAKE_BINARY_DIR}/resources/dlna/MediaRenderer.xml COPYONLY)
configure_file(resources/dlna/avt-scpd.xml
               ${CMAKE_BINARY_DIR}/resources/dlna/avt-scpd.xml COPYONLY)
configure_file(resources/dlna/rc-scpd.xml
               ${CMAKE_BINARY_DIR}/resources/dlna/rc-scpd.xml COPYONLY)
configure_file(resources/dlna/cm-scpd.xml
               ${CMAKE_BINARY_DIR}/resources/dlna/cm-scpd.xml COPYONLY)

# Tests
enable_testing()
add_subdirectory(tests)
