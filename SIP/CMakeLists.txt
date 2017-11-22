#
# OpenBTS-UMTS/SIP/CMakeLists.txt
#

add_compile_options(-Wextra)

find_package(PkgConfig)

pkg_check_modules(OSIP2 libosip2)
add_compile_options(${OSIP2_CFLAGS})
link_directories(${OSIP2_LIBRARY_DIRS})

pkg_check_modules(ORTP ortp)
add_compile_options(${ORTP_CFLAGS})
link_directories(${ORTP_LIBRARY_DIRS})

add_library(openbts-umts-sip
	SIPEngine.cpp
	SIPInterface.cpp
	SIPMessage.cpp
	SIPUtility.cpp
)

target_link_libraries(openbts-umts-sip
	${OSIP2_LIBRARIES}
	${ORTP_LIBRARIES}
)

add_dependencies(openbts-umts-sip openbts-umts-asn-generated)
