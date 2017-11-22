#
# OpenBTS-UMTS/TransceiverUHD/CMakeLists.txt
#

find_package(Boost REQUIRED COMPONENTS system)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

add_compile_options(-pthread -march=native)

add_library(transceiver-uhd-lib
	RadioInterface.cpp
	Resampler.cpp
	SampleBuffer.cpp
	Transceiver.cpp
	UHDDevice.cpp
	convert.c
	convolve.c
)

add_executable(transceiver runTransceiver.cpp ../apps/GetConfigurationKeys.cpp)

target_link_libraries(transceiver
	transceiver-uhd-lib
	uhd
	openbts-umts-gsm
	openbts-umts-umts
	openbts-umts-common
	sqlite3
	Boost::system
	-pthread
)

install(TARGETS transceiver
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
