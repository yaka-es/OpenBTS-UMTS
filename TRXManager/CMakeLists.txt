#
# OpenBTS-UMTS/TRXManager/CMakeLists.txt
#

add_library(openbts-umts-trxmanager
	TRXManager.cpp
)

add_dependencies(openbts-umts-trxmanager ${openbts_deps_prebuild})

# README.TRXManager
# clockdump.sh
