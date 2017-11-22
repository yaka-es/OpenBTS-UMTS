#
# OpenBTS-UMTS/Control/CMakeLists.txt
#

add_library(openbts-umts-control
	CallControl.cpp
	ControlCommon.cpp
	DCCHDispatch.cpp
	MobilityManagement.cpp
	RadioResource.cpp
	SMSControl.cpp
	TMSITable.cpp
	TransactionTable.cpp	
)

target_link_libraries(openbts-umts-control openbts-umts-asn)

#add_executable(RRLP_PDU_Test RRLP_PDU_Test.cpp)
#target_link_libraries(RRLP_PDU_Test openbts-umts-control)
