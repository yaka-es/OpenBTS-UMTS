/* -*- c++ -*- */

/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 * USRP - Universal Software Radio Peripheral
 *
 * Copyright 2003, 2004, 2009 Free Software Foundation, Inc.
 * Copyright 2014 Range Networks, Inc.
 * 
 * This software is distributed under the terms of the GNU General Public 
 * License version 3. See the COPYING and NOTICE files in the current
 * directory for licensing information.
 * 
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>

#include <Logger.h>
#include <Configuration.h>

ConfigurationTable gConfig;

using namespace std;

#include "rnrad1Core.h"

char *prog_name;

static void set_progname(char *path)
{
	char *p = strrchr(path, '/');
	if (p != 0)
		prog_name = p + 1;
	else
		prog_name = path;
}

static void usage()
{
	fprintf(stderr, "usage: \n");
	fprintf(stderr, "  %s [-v] [-w <which_board>] [-x] serialnumber\n", prog_name);
	exit(1);
}

#if 0
static void die (const char *msg)
{
	fprintf(stderr, "%s (die): %s\n", prog_name, msg);
	exit(1);
}
#endif

static int hexval(char ch)
{
	if ('0' <= ch && ch <= '9')
		return ch - '0';

	if ('a' <= ch && ch <= 'f')
		return ch - 'a' + 10;

	if ('A' <= ch && ch <= 'F')
		return ch - 'A' + 10;

	return -1;
}

unsigned char * hex_string_to_binary(const char *string, int *lenptr)
{
	int sl = strlen(string);
	if (sl & 0x01) {
		fprintf(stderr, "%s: odd number of chars in <hex-string>\n", prog_name);
		return 0;
	}

	int len = sl / 2;
	*lenptr = len;
	unsigned char *buf = new unsigned char[len];

	for (int i = 0; i < len; i++) {
		int hi = hexval (string[2 * i + 0]);
		int lo = hexval (string[2 * i + 1]);
		if (hi < 0 || lo < 0){
			fprintf(stderr, "%s: invalid char in <hex-string>\n", prog_name);
			delete[] buf;
			return 0;
		}
		buf[i] = (hi << 4) | lo;
	}
	return buf;
}

void print_hex(FILE *fp, unsigned char *buf, int len)
{
	for (int i = 0; i < len; i++) {
		fprintf(fp, "%02x", buf[i]);
	}
	fprintf(fp, "\n");
}

static void chk_result(bool ok)
{
	if (!ok) {
		fprintf(stderr, "%s: failed\n", prog_name);
		exit(1);
	}
}

bool get_on_off(const char *s)
{
	if (strcmp(s, "on") == 0)
		return true;

	if (strcmp(s, "off") == 0)
		return false;

	usage(); // no return
	return false;
}


int main(int argc, char **argv)
{
	int ch;
	//bool verbose = false;
	int which_board = 0;
	//bool fx2_ok_p = false;

	set_progname(argv[0]);

	while ((ch = getopt(argc, argv, "vw:x")) != EOF){
		switch (ch) {
		case 'v':
			//verbose = true;
			break;

		case 'w':
			which_board = strtol(optarg, 0, 0);
			break;

		case 'x':
			//fx2_ok_p = true;
			break;

		default:
			fprintf(stderr, "Bad option: %c\n", ch);
			usage();
			break;
		}
	}

	int nopts = argc - optind;

	fprintf(stderr, "nopts: %d", nopts);

	if (nopts < 1)
		usage();

	gLogInit("openbts", argv[1], LOG_LOCAL7);

	rnrad1Core *core = new rnrad1Core(
		which_board,
		RAD1_CMD_INTERFACE,
		RAD1_CMD_ALTINTERFACE,
		"", "", true);

	if (nopts != 1) {
		usage();
	} else {
		char *hex_string  = argv[optind];
		int len;
		unsigned char *buf = (unsigned char *)hex_string;
		len = 8;

		//hex_string_to_binary (hex_string, &len);

		if (buf == 0)
			chk_result(0);

		std::string bufStr;
		bufStr.assign((const char *)buf, len);

		bool result = core->writeEeprom(0x50, 248, bufStr);
		chk_result(result);
	}

	delete core;

	return 0;
}
