# iPad uses the shared engine and its network radio backends. Desktop helper
# processes, USB drivers and host-installed libraries are not iOS dependencies.
set(AETHER_BUILD_TESTS OFF CACHE BOOL "Build the AetherSDR test harnesses")
set(ENABLE_DSTAR OFF CACHE BOOL "Build the external digital voice helper")
set(ENABLE_RTL OFF CACHE BOOL "Build the USB RTL-SDR backend")
set(ENABLE_RADE OFF CACHE BOOL "Build RADE digital voice")
set(ENABLE_ASR OFF CACHE BOOL "Build on-device speech recognition")
set(ENABLE_DFNR OFF CACHE BOOL "Build DeepFilterNet3")
set(ENABLE_MQTT OFF CACHE BOOL "Build MQTT support")
if(ENABLE_DSTAR OR ENABLE_RTL OR ENABLE_DFNR)
    message(FATAL_ERROR "The iPad target does not yet support the external D-STAR helper, USB RTL-SDR, or desktop DeepFilterNet binaries. Set ENABLE_DSTAR, ENABLE_RTL and ENABLE_DFNR to OFF.")
endif()
if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 18.0)
    message(FATAL_ERROR "The experimental iPad target requires iPadOS 18 or later.")
endif()
# Do load FindPkgConfig (Qt's dependency finders call its macros), but never
# let a host pkg-config resolve macOS libraries into an iOS link.
set(PKG_CONFIG_EXECUTABLE /usr/bin/false CACHE FILEPATH "No host pkg-config for iOS")
set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2")
set(AETHER_IOS_BUNDLE_ID "com.aethersdr.ipad" CACHE STRING "iPad app bundle identifier")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO" CACHE STRING
    "Allow Xcode signing; turn on when using a development team")

set(AETHER_IOS_DEPS "" CACHE PATH "FFTW prefix from setup-ios-deps.sh for this SDK")
if(AETHER_IOS_DEPS)
    find_path(FFTW3_INC fftw3.h PATHS "${AETHER_IOS_DEPS}/include"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FFTW3_LIB fftw3 PATHS "${AETHER_IOS_DEPS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(FFTW3F_LIB fftw3f PATHS "${AETHER_IOS_DEPS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(OPUS_LIB opus PATHS "${AETHER_IOS_DEPS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_path(OPUS_INC opus/opus.h PATHS "${AETHER_IOS_DEPS}/include"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
