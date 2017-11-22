#
# OpenBTS-UMTS/UMTS/CMakeLists.txt
#

include_directories(../TransceiverRAD1)

add_library(openbts-umts-umts
	AsnHelper.cpp
	IntegrityProtect.cpp
	MACEngine.cpp
	RateMatch.cpp
	UMTSCLI.cpp
	UMTSCodes.cpp
	UMTSCommon.cpp
	UMTSConfig.cpp
	UMTSL1CC.cpp
	UMTSL1Const.cpp
	UMTSL1FEC.cpp
	UMTSLogicalChannel.cpp
	UMTSPhCh.cpp
	UMTSRadioModem.cpp
	UMTSRadioModemSequences.cpp
	UMTSTransfer.cpp
	URLC.cpp
	URRC.cpp
	URRCMessages.cpp
	URRCTrCh.cpp
	sigProcLib.cpp
)

add_dependencies(openbts-umts-umts ${openbts_deps_prebuild})

# README.TRXManager
# clockdump.sh
