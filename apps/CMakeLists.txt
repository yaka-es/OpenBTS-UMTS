#
# OpenBTS-UMTS/apps/CMakeLists.txt
#

#find_package(PkgConfig REQUIRED)

#pkg_check_modules(ZMQ libzmq)
#add_compile_options(${ZMQ_CFLAGS})
#include_directories(${ZMQ_INCLUDE_DIRS})
#link_directories(${ZMQ_LIBRARY_DIRS})

#pkg_check_modules(ORTP ortp)
#add_compile_options(${ORTP_CFLAGS})
#link_directories(${ORTP_LIBRARY_DIRS})

include_directories(${BOOST_INCLUDE_DIRS})

add_compile_options(-pthread)

add_executable(OpenBTS-UMTS OpenBTS-UMTS.cpp GetConfigurationKeys.cpp)
target_link_libraries(OpenBTS-UMTS
	${LINK_FLAGS_LTO}
	openbts-umts-globals
	openbts-umts-cli
	openbts-umts-trxmanager
	openbts-umts-sip
	openbts-umts-umts
	openbts-umts-control
	openbts-umts-sgsnggsn
	openbts-umts-asn
	openbts-umts-gsm
	openbts-umts-sms
	openbts-nodemanager
	#${OSIP_LIBS}
	#${ORTP_LIBS}
	openbts-umts-common
	zmq
	-pthread
)
add_dependencies(OpenBTS-UMTS ${openbts_deps_prebuild})

add_executable(OpenBTS-UMTSCLI OpenBTS-UMTSCLI.cpp)
target_link_libraries(OpenBTS-UMTSCLI
	${LINK_FLAGS_LTO}
	readline
	-pthread
)
add_dependencies(OpenBTS-UMTSCLI ${openbts_deps_prebuild})

add_executable(OpenBTS-UMTSDo OpenBTS-UMTSDo.cpp)
target_link_libraries(OpenBTS-UMTSDo
	${LINK_FLAGS_LTO}
	-pthread
)
add_dependencies(OpenBTS-UMTSDo ${openbts_deps_prebuild})

# OpenBTS-UMTS.example.sql
# openbts-umts.conf

install(TARGETS
		OpenBTS-UMTS
		OpenBTS-UMTSCLI
		OpenBTS-UMTSDo
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
