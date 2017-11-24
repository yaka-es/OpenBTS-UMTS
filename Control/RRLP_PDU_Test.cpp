/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 *
 * Copyright 2014 Range Networks, Inc.
 *
 * This software is distributed under the terms of the GNU Affero General
 * Public License version 3. See the COPYING and NOTICE files in the main
 * directory for licensing information.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 */

#include <CommonLibs/Configuration.h>

#include "RRLPQueryController.h"

using namespace GSM::RRLP;

// compile one liner:
// g++ -DRRLP_TEST_HACK -L../GSM/.libs -L../CommonLibs/.libs -I../CLI -I../Globals -I../GSM -I../CommonLibs
// RRLPQueryController.cpp RRLP_PDU_Test.cpp -lcommon -lGSM -lpthread -o RRLP_PDU_Test

// Required by various stuff - I think a TODO is to allow tests to work without
// having to copy this line around.
ConfigurationTable gConfig("OpenBTS.config");

int main()
{
	// This is a MsrPositionRsp with valid coordinates
	// 38.28411340713501,237.95414686203003,22 (as parsed by the erlang automatically generated
	// implementation - checked against a map).
	BitVector test(
		"000001100011100000000000000101100010001000010001111111111111111101001010111110111101111010110110010000"
		"001101011011010011100011101010001111001001010011100000000001000100000110000001100011001000001000010001"
		"0000");
	RRLPQueryController c(test);
	return 0;
}
