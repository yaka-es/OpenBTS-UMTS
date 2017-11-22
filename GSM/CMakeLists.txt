#
# OpenBTS-UMTS/GSM/CMakeLists.txt
#

add_library(openbts-umts-gsm
	GSMCommon.cpp
	GSML3CCElements.cpp
	GSML3CCMessages.cpp
	GSML3CommonElements.cpp
	GSML3MMElements.cpp
	GSML3MMMessages.cpp
	GSML3Message.cpp
	GSML3RRElements.cpp
	GSML3RRMessages.cpp
	GSMTAPDump.cpp
	GSMTransfer.cpp
)

#target_link_libraries(openbts-umts-gsm)
