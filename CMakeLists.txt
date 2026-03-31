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
    avahi-common
  )
endif()

# libupnp (pupnp) — DLNA SSDP advertisement (DISC-03)
pkg_check_modules(UPNP REQUIRED IMPORTED_TARGET libupnp)

# spdlog — optional, fall back to GLib logging if absent
find_package(spdlog QUIET)

# OpenSSL — link now to avoid header-change task in future phases (per RESEARCH.md Standard Stack)
find_package(OpenSSL REQUIRED)

# Protobuf — system package 3.21.12 (NOT CONFIG mode — per Pitfall 5 in RESEARCH.md)
# Headers are vendored to /tmp/protobuf-dev when libprotobuf-dev is not system-installed.
list(APPEND CMAKE_PREFIX_PATH "/tmp/protobuf-dev/usr")
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf version: ${Protobuf_VERSION}")
message(STATUS "Protobuf include dirs: ${Protobuf_INCLUDE_DIRS}")

# CASTV2 protobuf code generation
# Generated files land in ${CMAKE_CURRENT_BINARY_DIR}; added to airshow and test_cast targets.
protobuf_generate_cpp(CAST_PROTO_SRCS CAST_PROTO_HDRS src/cast/cast_channel.proto)
# Make available to tests/CMakeLists.txt via cache
set(CAST_PROTO_SRCS "${CAST_PROTO_SRCS}" CACHE INTERNAL "CASTV2 protobuf generated sources")
set(CAST_PROTO_HDRS "${CAST_PROTO_HDRS}" CACHE INTERNAL "CASTV2 protobuf generated headers")

# GStreamer WebRTC (for Plan 02 WebRTC media pipeline — link now, headers used later)
pkg_check_modules(GST_WEBRTC IMPORTED_TARGET
  gstreamer-webrtc-1.0>=1.20
  gstreamer-sdp-1.0>=1.20
)

# libplist — required by UxPlay lib/ for Apple plist parsing (D-07)
pkg_check_modules(PLIST REQUIRED IMPORTED_TARGET libplist-2.0)

# UxPlay lib/ — builds the 'airplay' static library target (D-01)
# Include lib/ directly to skip uxplay's renderers/ and uxplay.cpp (D-03)
# Must add llhttp and playfair subdirs first — airplay target links against them
# (mirrors the order in vendor/uxplay/CMakeLists.txt: llhttp, playfair, lib)
# Pre-set OPENSSL_FOUND so UxPlay's find_package(OpenSSL 1.1.1) uses already-found OpenSSL
set(OPENSSL_FOUND TRUE)
add_subdirectory(vendor/uxplay/lib/llhttp)
add_subdirectory(vendor/uxplay/lib/playfair)
add_subdirectory(vendor/uxplay/lib)

# Main executable
qt_add_executable(airshow
  src/main.cpp
  src/pipeline/MediaPipeline.cpp
  src/ui/ReceiverWindow.cpp
  src/ui/AudioBridge.cpp
  src/ui/ConnectionBridge.cpp
  src/ui/SettingsBridge.cpp
  src/protocol/ProtocolManager.cpp
  src/protocol/AirPlayHandler.cpp
  src/protocol/DlnaHandler.cpp
  src/protocol/CastHandler.cpp
  src/protocol/CastSession.cpp
  src/protocol/MiracastHandler.cpp
  ${CAST_PROTO_SRCS}
  src/settings/AppSettings.cpp
  src/security/SecurityManager.cpp
  src/discovery/ServiceAdvertiser.cpp
  src/discovery/AvahiAdvertiser.cpp
  src/discovery/DiscoveryManager.cpp
  src/discovery/UpnpAdvertiser.cpp
  src/platform/WindowsFirewall.cpp
)

qt_add_qml_module(airshow
  URI AirShow
  VERSION 1.0
  QML_FILES
    qml/main.qml
    qml/HudOverlay.qml
    qml/IdleScreen.qml
    qml/ApprovalDialog.qml
)

target_include_directories(airshow PRIVATE
  src
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
  PkgConfig::UPNP
  OpenSSL::SSL
  OpenSSL::Crypto
  airplay
  PkgConfig::PLIST
  ${Protobuf_LIBRARIES}
)

if(GST_WEBRTC_FOUND)
  target_link_libraries(airshow PRIVATE PkgConfig::GST_WEBRTC)
  target_compile_definitions(airshow PRIVATE GST_USE_UNSTABLE_API)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(airshow PRIVATE PkgConfig::AVAHI)
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
