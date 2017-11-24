#
# OpenBTS-UMTS/TransceiverRAD1/CMakeLists.txt
#

add_compile_options(-pthread)

add_definitions(-DOMNITHREAD_POSIX=1)

add_library(transceiver-rad1-lib
	DummyLoad.cpp
	FactoryCalibration.cpp
	RAD1Device.cpp
	Transceiver.cpp
	radioInterface.cpp
	rnrad1Core.cpp
#	sigProcLib.cpp # using UMTS/sigProcLib.cpp instead
        fusb.cpp
        rnrad1Rx.cpp
        rnrad1Tx.cpp
)

target_link_libraries(transceiver-rad1-lib usb-1.0)
add_dependencies(transceiver-rad1-lib ${openbts_deps_prebuild})

add_executable(transceiver-rad1 runTransceiver.cpp ../apps/GetConfigurationKeys.cpp)
add_executable(RAD1ping RAD1ping.cpp)
add_executable(RAD1Cmd RAD1Cmd.cpp)
add_executable(RAD1SN RAD1SN.cpp)

set(link_libs1 transceiver-rad1-lib)
set(link_libs2
	openbts-umts-common
	openbts-umts-gsm
	sqlite3
	-pthread
)
target_link_libraries(transceiver-rad1 ${link_libs1} openbts-umts-gsm openbts-umts-umts ${link_libs2})
target_link_libraries(RAD1ping ${link_libs1} ${link_libs2})
target_link_libraries(RAD1Cmd ${link_libs1} ${link_libs2})
target_link_libraries(RAD1SN ${link_libs1} ${link_libs2})

# README
# README.Talgorithm

install(TARGETS transceiver-rad1
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
