#
# OpenBTS-UMTS/CLI/CMakeLists.txt
#

add_library(openbts-umts-cli
	CLI.cpp
)

add_dependencies(openbts-umts-cli ${openbts_deps_prebuild})

# README.CLI 
