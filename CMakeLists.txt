cmake_minimum_required(VERSION 3.20)
project(sovkit_stun VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

# Use a public, installed libuv. No dependency download or SovKit SDK required.
include(cmake/StunLibuv.cmake)
if(NOT TARGET SovkitStun::libuv)
  message(FATAL_ERROR "libuv >= 1.44 is required (CMake package or pkg-config)")
endif()

add_library(sovkit_stun STATIC src/libnet_stun_server.cc src/endpoint.cc)
add_library(SovkitStun::stun ALIAS sovkit_stun)
set_target_properties(sovkit_stun PROPERTIES OUTPUT_NAME sovkit-stun
                      EXPORT_NAME stun POSITION_INDEPENDENT_CODE ON)
target_compile_features(sovkit_stun PUBLIC cxx_std_20)
target_include_directories(sovkit_stun PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sovkit-stun>)
target_link_libraries(sovkit_stun PUBLIC SovkitStun::libuv Threads::Threads)

add_executable(stun_server src/main.cc)
set_target_properties(stun_server PROPERTIES OUTPUT_NAME sovkit-stun)
target_compile_definitions(stun_server PRIVATE STUN_VERSION="${PROJECT_VERSION}")
target_link_libraries(stun_server PRIVATE SovkitStun::stun)

option(STUN_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(STUN_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "STUN_SANITIZERS requires Clang or GCC")
  endif()
  target_compile_options(sovkit_stun PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(sovkit_stun PUBLIC -fsanitize=address,undefined)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(sovkit_stun PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(stun_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
  add_executable(stun_codec_test tests/codec_test.cc)
  target_link_libraries(stun_codec_test PRIVATE SovkitStun::stun)
  add_test(NAME codec COMMAND stun_codec_test)
  add_test(NAME source_manifest COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_import.py)
  add_test(NAME udp_service COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/service_test.py $<TARGET_FILE:stun_server>)
  set_tests_properties(udp_service PROPERTIES TIMEOUT 100)
  add_test(NAME installed_sdk COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_test.py
    --cmake ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}
    --config $<CONFIG> --compiler ${CMAKE_CXX_COMPILER}
    "--dependency-prefix=${CMAKE_PREFIX_PATH}")
  set_tests_properties(installed_sdk PROPERTIES TIMEOUT 120)
  add_test(NAME package_checks COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_test.py)
endif()

# Installation is opt-in and never enables a service or overwrites a config.
install(TARGETS stun_server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS sovkit_stun EXPORT SovkitStunTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/libnet_stun_server.h include/libnet_uv.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sovkit-stun)
install(EXPORT SovkitStunTargets NAMESPACE SovkitStun::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SovkitStun)
configure_package_config_file(cmake/SovkitStunConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/SovkitStunConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SovkitStun)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SovkitStunConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SovkitStunConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/SovkitStunConfigVersion.cmake cmake/StunLibuv.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SovkitStun)
install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/sovkit-stun)
install(FILES packaging/server.conf packaging/sovkit-stun.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/sovkit-stun/examples)
install(FILES examples/sdk/CMakeLists.txt examples/sdk/main.cc
        DESTINATION ${CMAKE_INSTALL_DATADIR}/sovkit-stun/examples/sdk)
