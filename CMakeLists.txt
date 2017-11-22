#
# OpenBTS-UMTS/CMakeLists.txt
#

cmake_minimum_required(VERSION 3.0)

project(OpenBTS-UMTS)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

enable_testing()

include(cmake/features.cmake)

find_package(Boost COMPONENTS system REQUIRED)
find_library(USB usb-1.0 REQUIRED)
find_library(ZMQ zmq REQUIRED)

add_compile_options(-g)
add_compile_options(-O3)
add_compile_options(-Wall)
add_compile_options(-Werror)
#add_compile_options(-Weffc++)
add_compile_options(-pthread)

set(TIMESTAMP_ISO "1900-01-01T00:00:00")
add_definitions(-DTIMESTAMP_ISO="${TIMESTAMP_ISO}")
add_definitions(-DREPO_REV="unknown")

configure_file(config-cmake.h.in config.h)
add_definitions(-DHAVE_CONFIG_H)

include_directories(${PROJECT_SOURCE_DIR})  # required in apps/OpenBTS.cpp
include_directories(${PROJECT_BINARY_DIR})  # required for <config.h>
include_directories(${PROJECT_BINARY_DIR}/ASN/generated)

include_directories(CLI)
include_directories(CommonLibs)
include_directories(Control)
include_directories(GSM)
include_directories(Globals)
include_directories(NodeManager)
include_directories(NodeManager/JsonBox-0.4.3/include)
include_directories(SGSNGGSN)
include_directories(SIP)
include_directories(SMS)
include_directories(TRXManager)
include_directories(UMTS)

set(openbts_deps_prebuild openbts-umts-asn-generated)

add_subdirectory(ASN)		# openbts-umts-asn
add_subdirectory(CLI)		# openbts-umts-cli
add_subdirectory(CommonLibs)	# openbts-umts-common
add_subdirectory(Control)	# openbts-umts-control
add_subdirectory(GSM)		# openbts-umts-gsm
add_subdirectory(Globals)	# openbts-umts-globals
add_subdirectory(NodeManager)	# openbts-umts-nodemanager
add_subdirectory(SGSNGGSN)	# openbts-umts-sgsnggsn
add_subdirectory(SIP)		# openbts-umts-sip
add_subdirectory(SMS)		# openbts-umts-sms
add_subdirectory(TRXManager)	# openbts-umts-trxmanager
add_subdirectory(UMTS)		# openbts-umts-umts

add_subdirectory(TransceiverRAD1)
add_subdirectory(TransceiverUHD)
add_subdirectory(apps)
