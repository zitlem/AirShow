find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(test_pipeline
  test_pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/pipeline/MediaPipeline.cpp
)

target_include_directories(test_pipeline PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_pipeline PRIVATE
  GTest::gtest_main
  GTest::gmock
  PkgConfig::GST
)

# Register test filters matching VALIDATION.md per-task verification map
gtest_discover_tests(test_pipeline
  TEST_SUFFIX ""
  PROPERTIES TIMEOUT 30
)

add_executable(test_discovery
  test_discovery.cpp
  ${CMAKE_SOURCE_DIR}/src/settings/AppSettings.cpp
  ${CMAKE_SOURCE_DIR}/src/platform/WindowsFirewall.cpp
)

target_include_directories(test_discovery PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(test_discovery PRIVATE
  BUILD_DIR="${CMAKE_BINARY_DIR}"
)

target_link_libraries(test_discovery PRIVATE
  GTest::gtest_main
  GTest::gmock
  Qt6::Core
)

gtest_discover_tests(test_discovery
  TEST_SUFFIX ""
  PROPERTIES TIMEOUT 30
)

add_executable(test_display
  test_display.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/ConnectionBridge.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/SettingsBridge.cpp
  ${CMAKE_SOURCE_DIR}/src/settings/AppSettings.cpp
)

target_include_directories(test_display PRIVATE
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_display PRIVATE
  GTest::gtest_main
  GTest::gmock
  Qt6::Core
)

gtest_discover_tests(test_display
  TEST_SUFFIX ""
  PROPERTIES TIMEOUT 30
)

add_executable(test_airplay
  test_airplay.cpp
  ${CMAKE_SOURCE_DIR}/src/protocol/AirPlayHandler.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/ConnectionBridge.cpp
  ${CMAKE_SOURCE_DIR}/src/pipeline/MediaPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/discovery/DiscoveryManager.cpp
  ${CMAKE_SOURCE_DIR}/src/discovery/ServiceAdvertiser.cpp
  ${CMAKE_SOURCE_DIR}/src/discovery/AvahiAdvertiser.cpp
  ${CMAKE_SOURCE_DIR}/src/settings/AppSettings.cpp
)

target_include_directories(test_airplay PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/vendor/uxplay/lib
)

target_link_libraries(test_airplay PRIVATE
  GTest::GTest GTest::Main
  Qt6::Core
  PkgConfig::GST
  airplay
  PkgConfig::PLIST
  OpenSSL::SSL OpenSSL::Crypto
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(test_airplay PRIVATE PkgConfig::AVAHI)
endif()

add_test(NAME test_airplay COMMAND test_airplay)

gtest_discover_tests(test_airplay
  TEST_SUFFIX ""
  PROPERTIES TIMEOUT 30
)

add_executable(test_dlna test_dlna.cpp)
target_include_directories(test_dlna PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dlna PRIVATE GTest::gtest_main)
gtest_discover_tests(test_dlna TEST_SUFFIX "" PROPERTIES TIMEOUT 30)
