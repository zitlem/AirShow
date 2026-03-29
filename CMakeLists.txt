cmake_minimum_required(VERSION 3.28)
project(MyAirShow LANGUAGES CXX C)  # C required for UxPlay lib/ C sources

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt6 — must come before GStreamer to add Qt6's FindGStreamer.cmake to the module path
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml)
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
qt_add_executable(myairshow
  src/main.cpp
  src/pipeline/MediaPipeline.cpp
  src/ui/ReceiverWindow.cpp
  src/ui/AudioBridge.cpp
  src/ui/ConnectionBridge.cpp
  src/ui/SettingsBridge.cpp
  src/protocol/ProtocolManager.cpp
  src/protocol/AirPlayHandler.cpp
  src/protocol/DlnaHandler.cpp
  src/settings/AppSettings.cpp
  src/discovery/ServiceAdvertiser.cpp
  src/discovery/AvahiAdvertiser.cpp
  src/discovery/DiscoveryManager.cpp
  src/discovery/UpnpAdvertiser.cpp
  src/platform/WindowsFirewall.cpp
)

qt_add_qml_module(myairshow
  URI MyAirShow
  VERSION 1.0
  QML_FILES
    qml/main.qml
    qml/HudOverlay.qml
    qml/IdleScreen.qml
)

target_include_directories(myairshow PRIVATE src vendor/uxplay/lib)

target_link_libraries(myairshow PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
  PkgConfig::GST
  PkgConfig::UPNP
  OpenSSL::SSL
  OpenSSL::Crypto
  airplay
  PkgConfig::PLIST
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(myairshow PRIVATE PkgConfig::AVAHI)
endif()

if(spdlog_FOUND)
  target_link_libraries(myairshow PRIVATE spdlog::spdlog)
  target_compile_definitions(myairshow PRIVATE HAVE_SPDLOG)
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
