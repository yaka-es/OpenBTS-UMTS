#
# OpenBTS-UMTS/SGSNGGSN/CMakeLists.txt
#

add_library(openbts-umts-sgsnggsn
	GPRSL3Messages.cpp
	Ggsn.cpp
	LLC.cpp
	Sgsn.cpp
	SgsnCli.cpp
	iputils.cpp
	miniggsn.cpp
)
