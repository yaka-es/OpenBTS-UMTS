/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 *
 * Copyright 2009, 2010 Free Software Foundation, Inc.
 * Copyright 2010 Kestrel Signal Processing, Inc.
 * Copyright 2011, 2012, 2014 Range Networks, Inc.
 *
 * This software is distributed under the terms of the GNU Affero General
 * Public License version 3. See the COPYING and NOTICE files in the main
 * directory for licensing information.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm> // for sort()
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>

#include "config.h"

#include <CommonLibs/Logger.h>
#include <CommonLibs/MemoryLeak.h>
#include <Control/TransactionTable.h>
#include <Globals/Globals.h>
#include <UMTS/UMTSConfig.h>
#include <UMTS/UMTSLogicalChannel.h>

#include "CLI.h"

#undef WARNING

using namespace std;
using namespace CommandLine;

// (mike) Just following the example of SGSN because of linker errors but why do we do this?
namespace UMTS {
extern CommandLine::CLIStatus rrcTest(int argc, char **argv, std::ostream &os);
extern CommandLine::CLIStatus rlcTest(int argc, char **argv, std::ostream &os);
}; // namespace UMTS
namespace SGSN {
// Hack.
extern CommandLine::CLIStatus sgsnCLI(int argc, char **argv, std::ostream &os);
}; // namespace SGSN

extern TransceiverManager gTRX;
// extern NodeManager gNodeManager;

namespace CommandLine {
using namespace std;
using namespace Control;

/** Standard responses in the CLI, much mach erorrCode enum. */
static const char *standardResponses[] = {
	"success",			 // 0
	"wrong number of arguments",     // 1
	"bad argument(s)",		 // 2
	"command not found",		 // 3
	"too many arguments for parser", // 4
	"command failed",		 // 5
};

struct CLIParseError {
	string msg;
	CLIParseError(string wMsg) : msg(wMsg) {}
};

CLIStatus Parser::execute(char *line, ostream &os) const
{
	LOG(INFO) << "executing console command: " << line;

	// tokenize
	char *argv[mMaxArgs];
	int argc = 0;
	// This is (almost) straight from the man page for strsep.
	while (line && argc < mMaxArgs) {
		while (*line == ' ') {
			line++;
		}
		if (!*line) {
			break;
		}
		char *anarg = line;
		if (*line == '"') { // We allow a quoted string as a single argument.  Quotes themselves are removed.
			line++;
			anarg++;
			char *endquote = strchr(line, '"');
			if (endquote == NULL) {
				os << "error: Missing quote." << endl;
				return FAILURE;
			}
			if (!(endquote[1] == 0 || endquote[1] == ' ')) {
				os << "error: Embedded quotes not allowed." << endl;
				return FAILURE;
			}
			*endquote = 0;
			line = endquote + 1;
		} else if (strsep(&line, " ") == NULL) {
			break;
		}
		argv[argc++] = anarg;
	}
	// for (ap=argv; (*ap=strsep(&line," ")) != NULL; ) {
	//	if (**ap != '\0') {
	//		if (++ap >= &argv[mMaxArgs]) break;
	//		else argc++;
	//	}
	//}
	// Blank line?
	if (!argc)
		return SUCCESS;
	// Find the command.
	// printf("args=%d\n",argc);
	// for (int i = 0; i < argc; i++) {
	//	printf("argv[%d]=%s\n",i,argv[i]);
	//}
	ParseTable::const_iterator cfp = mParseTable.find(argv[0]);
	if (cfp == mParseTable.end()) {
		return NOT_FOUND;
	}
	CLICommand func;
	func = cfp->second;
	// Do it.
	CLIStatus retVal;
	try {
		retVal = (*func)(argc, argv, os);
	} catch (CLIParseError &pe) {
		os << pe.msg << endl;
		retVal = SUCCESS; // Dont print any further messages.
	}
	// Give hint on bad # args.
	if (retVal == BAD_NUM_ARGS)
		os << help(argv[0]) << endl;
	return retVal;
}

CLIStatus Parser::process(const char *line, ostream &os) const
{
	static Mutex oneCommandAtATime;
	ScopedLock lock(oneCommandAtATime);
	char *newLine = strdup(line);
	CLIStatus retVal = execute(newLine, os);
	free(newLine);
	if (retVal < 0 || retVal > FAILURE) {
		os << "Unrecognized CLI command exit status: " << retVal << endl;
	} else if (retVal != SUCCESS) {
		os << standardResponses[retVal] << endl;
	}
	return retVal;
}

static void *commandLineFunc(void *arg)
{
	const char *prompt = "OpenBTS> "; // gConfig.getStr("CLI.Prompt");  CLI.Prompt no longer defined.
	try {
#ifdef HAVE_READLINE
		using_history();
		while (true) {
			clearerr(stdin); // Control-D may set the eof bit which causes getline to return immediately.
					 // Fix it.
			char *inbuf = readline(prompt);
			if (inbuf) {
				if (*inbuf) {
					add_history(inbuf);
					// The parser returns -1 on exit.
					if (gParser.process(inbuf, cout, cin) < 0) {
						free(inbuf);
						break;
					}
				}
				free(inbuf);
			} else {
				printf("EOF ignored\n");
			}
			sleep(1); // in case something goofs up here, dont steal all the cpu cycles.
		}
#else
		while (true) {
			// cout << endl <<
			cout << endl << prompt;
			cout.flush();
			char inbuf[1024];
			cin.clear(); // Control-D may set the eof bit which causes getline to return immediately.  Fix
				     // it.
			cin.getline(inbuf, 1024, '\n'); // istream::getline
			// The parser returns -1 on exit.
			if (gParser.process(inbuf, cout) < 0)
				break;
			sleep(1); // in case something goofs up here, dont steal all the cpu cycles.
		}
#endif
	} catch (ConfigurationTableKeyNotFound e) {
		LOG(EMERG) << "required configuration parameter " << e.key() << " not defined, aborting";
		//		gReports.incr("OpenBTS.Exit.Error.ConfigurationParamterNotFound");
	}

	printf("ALERT: exiting OpenBTS as directed by command line; exiting...\n");
	exit(0); // Exit OpenBTS
	return NULL;
}

void Parser::startCommandLine() // (pat) Start a simple command line processor as a separate thread.
{
	static Thread commandLineThread;
	commandLineThread.start(commandLineFunc, NULL);
}

const char *Parser::help(const string &cmd) const
{
	HelpTable::const_iterator hp = mHelpTable.find(cmd);
	if (hp == mHelpTable.end())
		return "no help available";
	return hp->second.c_str();
}

// Parse options in optstring out of argc,argv.
// The optstring is a space separated list of options.  The options need not start with '-'.  To recognize just "-" or
// "--" just add it in. Return a map containing the options found; if option in optstring was followed by ':', map value
// will be the next argv argument, otherwise "true". Leave argc,argv pointing at the first argument after the options,
// ie, on return argc is the number of non-option arguments remaining in argv. This routine does not allow combining
// options, ie, -a -b != -ab
#ifdef UNUSED
static map<string, string> cliParse(int &argc, char **&argv, ostream &os, const char *optstring)
{
	map<string, string> options; // The result
	// Skip the command name.
	argc--, argv++;
	// Parse args.
	for (; argc > 0; argc--, argv++) {
		char *arg = argv[0];
		// The argv to match may not contain ':' to prevent the pathological case, for example, where optionlist
		// contains "-a:" and command line arg is "-a:"
		if (strchr(arg, ':')) {
			return options;
		} // Thats the end of that.
		const char *op = strstr(optstring, arg);
		if (op && (op == optstring || op[-1] == ' ')) {
			const char *ep = op + strlen(arg);
			if (*ep == ':') {
				// This valid option requires an argument.
				argc--, argv++;
				if (argc <= 0) {
					throw CLIParseError(format("expected argument after: %s", arg));
				}
				options[arg] = string(argv[0]);
				continue;
			} else if (*ep == 0 || *ep == ' ') {
				// This valid option does not require an argument.
				options[arg] = string("true");
				continue;
			} else {
				// Partial match of something in optstring; drop through to treat it like any other
				// argument.
			}
		}
		// An argument beginning with - and not in optstring is an unrecognized option and is an error.
		if (*arg == '-') {
			throw CLIParseError(format("unrecognized argument: %s", arg));
		}
		return options;
	}
	return options;
}
#endif

/**@name Commands for the CLI. */
//@{

// forward refs
#ifdef UNUSED
static CLIStatus printStats(int argc, char **argv, ostream &os);
#endif

/*
	A CLI command takes the argument in an array.
	It returns 0 on success.
*/

/** Display system uptime and current GSM frame number. */
static CLIStatus uptime(int argc, char **argv, ostream &os)
{
	if (argc != 1)
		return BAD_NUM_ARGS;
	os.precision(2);

	time_t now = time(NULL);
	const char *timestring = ctime(&now);
	// no endl since ctime includes a "\n" in the string
	os << "Unix time " << now << ", " << timestring;
	int seconds = gNodeB.uptime();
	if (seconds < 120) {
		os << "uptime " << seconds << " seconds, frame " << gNodeB.time() << endl;
		return SUCCESS;
	}
	float minutes = seconds / 60.0F;
	if (minutes < 120) {
		os << "uptime " << minutes << " minutes, frame " << gNodeB.time() << endl;
		return SUCCESS;
	}
	float hours = minutes / 60.0F;
	if (hours < 48) {
		os << "uptime " << hours << " hours, frame " << gNodeB.time() << endl;
		return SUCCESS;
	}
	float days = hours / 24.0F;
	os << "uptime " << days << " days, frame " << gNodeB.time() << endl;

	return SUCCESS;
}

/** Give a list of available commands or describe a specific command. */
static CLIStatus showHelp(int argc, char **argv, ostream &os)
{
	if (argc == 2) {
		os << argv[1] << " " << gParser.help(argv[1]) << endl;
		return SUCCESS;
	}
	if (argc != 1)
		return BAD_NUM_ARGS;
	ParseTable::const_iterator cp = gParser.begin();
	os << endl << "Type \"help\" followed by the command name for help on that command." << endl << endl;
	int c = 0;
	const int cols = 3;
	while (cp != gParser.end()) {
		const string &wd = cp->first;
		os << wd << '\t';
		if (wd.size() < 8)
			os << '\t';
		++cp;
		c++;
		if (c % cols == 0)
			os << endl;
	}
	if (c % cols != 0)
		os << endl;
	return SUCCESS;
}

/** A function to return -1, the exit code for the caller. */
static CLIStatus exit_function(int argc, char **argv, ostream &os)
{
	unsigned wait = 0;
	if (argc > 2)
		return BAD_NUM_ARGS;
	if (argc == 2)
		wait = atoi(argv[1]);

	if (wait != 0)
		os << "waiting up to " << wait << " seconds for clearing of " << gNodeB.DTCHActive() << " active calls"
		   << endl;

	// Block creation of new channels.
	gNodeB.hold(true);
	// Wait up to the timeout for active channels to release.
	time_t finish = time(NULL) + wait;
	while (time(NULL) < finish) {
		unsigned load = gNodeB.DTCHActive();
		if (load == 0)
			break;
		sleep(1);
	}
	// bool loads = false;
	if (gNodeB.DTCHActive() > 0) {
		LOG(WARNING) << "dropping " << gNodeB.DTCHActive() << " DTCH channels on exit";
		// loads = true;
	}
	// if (loads) {
	//	os << endl << "exiting with loads:" << endl;
	//	printStats(1,NULL,os);
	//}
	os << endl << "exiting..." << endl;
	return CLI_EXIT;
}

/** Print or clear the TMSI table. */
static const char *tmsisHelp __attribute_used__ =
	"[-l | clear | dump [-l] <filename> | -delete -tmsi <tmsi> | -delete -imsi <imsi> | -query <query>] --\n"
	"   default print the TMSI table;  -l gives longer listing;\n"
	"   dump - dump the TMSI table to specified filename;\n"
	"   clear - clear the TMSI table;\n"
	"   -delete - delete entry for specified imsi or tmsi;\n"
	"   -query - run sql query, which may be quoted, eg: tmsis -query \"UPDATE TMSI_TABLE SET AUTH=0 WHERE "
	"IMSI=='123456789012'\" This option may be removed in future.";
/*
static CLIStatus tmsis(int argc, char** argv, ostream& os)
{
	map<string,string> options = cliParse(argc,argv,os,"-l dump: clear -delete -imsi: -tmsi: -query:");
	if (argc) return BAD_NUM_ARGS;
	if (options.count("clear")) {
		os << "clearing TMSI table" << endl;
		gTMSITable.tmsiTabClear();
		return SUCCESS;
	}
	if (options.count("dump")) {
		ofstream fileout;
		string filename = options["dump"];
		os << "dumping TMSI table to " << filename << endl;
		fileout.open(filename.c_str(), ios::out); // erases existing!
		gTMSITable.tmsiTabDump(options.count("-l"),fileout);
		return SUCCESS;
		//return dumpTMSIs(filename.c_str());
	}
	if (options.count("-delete")) {
		string tmsiopt = options["-tmsi"];
		string imsiopt = options["-imsi"];
		if (tmsiopt.size()) {
			unsigned tmsi = strtoul(tmsiopt.c_str(),NULL,0);
			if (gTMSITable.dropTmsi(tmsi)) {
				os << format("Deleted TMSI table entry for 0x%x",tmsi) << endl;
			} else {
				os << format("Cound not delete TMSI table entry for 0x%x",tmsi) << endl;
				return FAILURE;
			}
		} else if (imsiopt.size()) {
			if (gTMSITable.dropImsi(imsiopt.c_str())) {
				os << format("Deleted TMSI table entry for %s",imsiopt) << endl;
			} else {
				os << format("Cound not delete TMSI table entry for %s",imsiopt) << endl;
				return FAILURE;
			}
		} else {
			os << "tmsis delete expecting: tmsi or imsi value" << endl;
			return FAILURE;
		}
		return SUCCESS;
	}
	if (options.count("-query")) {
		string query = options["-query"];
		if (gTMSITable.runQuery(query.c_str(),0)) {
			os << "Query success."<<endl;
			return SUCCESS;
		} else {
			os << "Query failed:"<<query<<endl;
			return FAILURE;
		}
	}
	gTMSITable.tmsiTabDump(options.count("-l"),os);
	return SUCCESS;
}
*/

int isIMSI(const char *imsi)
{
	if (!imsi)
		return 0;
	if (strlen(imsi) != 15)
		return 0;

	for (unsigned i = 0; i < strlen(imsi); i++) {
		if (!isdigit(imsi[i]))
			return 0;
	}

	return 1;
}

/** Submit an SMS for delivery to an IMSI. */
static CLIStatus sendsimple(int argc, char **argv, ostream &os)
{
	if (argc < 4)
		return BAD_NUM_ARGS;

	char *IMSI = argv[1];
	char *srcAddr = argv[2];
	string rest = "";
	for (int i = 3; i < argc; i++)
		rest = rest + argv[i] + " ";
	const char *txtBuf = rest.c_str();

	if (!isIMSI(IMSI)) {
		os << "Invalid IMSI. Enter 15 digits only.";
		return BAD_VALUE;
	}

	static UDPSocket sock(0, "127.0.0.1", gConfig.getNum("SIP.Local.Port"));

	static const char form[] =
		"MESSAGE sip:IMSI%s@127.0.0.1 SIP/2.0\n"
		"Via: SIP/2.0/TCP 127.0.0.1;branch=%x\n"
		"Max-Forwards: 2\n"
		"From: %s <sip:%s@127.0.0.1:%d>;tag=%d\n"
		"To: sip:IMSI%s@127.0.0.1\n"
		"Call-ID: %x@127.0.0.1:%d\n"
		"CSeq: 1 MESSAGE\n"
		"Content-Type: text/plain\nContent-Length: %zu\n"
		"\n%s\n";
	static char buffer[1500];
	snprintf(buffer, 1499, form, IMSI, (unsigned)random(), srcAddr, srcAddr, sock.port(), (unsigned)random(), IMSI,
		(unsigned)random(), sock.port(), strlen(txtBuf), txtBuf);
	sock.write(buffer);

	os << "message submitted for delivery" << endl;

#if 0
	int numRead = sock.read(buffer,10000);
	if (numRead>=0) {
		buffer[numRead]='\0';
		os << "response: " << buffer << endl;
	} else {
		os << "timed out waiting for response";
	}
#endif

	return SUCCESS;
}

static CLIStatus cellID(int argc, char **argv, ostream &os)
{
	if (argc == 1) {
		os << "MCC=" << gConfig.getStr("UMTS.Identity.MCC") << " MNC=" << gConfig.getStr("UMTS.Identity.MNC")
		   << " LAC=" << gConfig.getNum("UMTS.Identity.LAC") << " CI=" << gConfig.getNum("UMTS.Identity.CI")
		   << endl;
		return SUCCESS;
	}

	if (argc != 5)
		return BAD_NUM_ARGS;

	// Safety check the args!!
	if (!gConfig.isValidValue("UMTS.Identity.MCC", argv[1])) {
		os << "MCC must be three digits" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("UMTS.Identity.MNC", argv[2])) {
		os << "MNC must be two or three digits" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("UMTS.Identity.LAC", argv[3])) {
		os << "Invalid value for LAC" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("UMTS.Identity.CI", argv[4])) {
		os << "Invalid value for CI" << endl;
		return BAD_VALUE;
	}

	//	gTMSITable.tmsiTabClear();
	gConfig.set("UMTS.Identity.MCC", argv[1]);
	gConfig.set("UMTS.Identity.MNC", argv[2]);
	gConfig.set("UMTS.Identity.LAC", argv[3]);
	gConfig.set("UMTS.Identity.CI", argv[4]);
	return SUCCESS;
}

/** Print or modify the global configuration table. */
static CLIStatus rawconfig(int argc, char **argv, ostream &os)
{
	// no args, just print
	if (argc == 1) {
		gConfig.find("", os);
		return SUCCESS;
	}

	// one arg, pattern match and print
	if (argc == 2) {
		gConfig.find(argv[1], os);
		return SUCCESS;
	}

	// >1 args: set new value
	string val;
	for (int i = 2; i < argc; i++) {
		val.append(argv[i]);
		if (i != (argc - 1))
			val.append(" ");
	}
	bool existing = gConfig.defines(argv[1]);
	string previousVal;
	if (existing) {
		previousVal = gConfig.getStr(argv[1]);
	}
	if (!gConfig.set(argv[1], val)) {
		os << "DB ERROR: " << argv[1] << " change failed" << endl;
		return FAILURE;
	}
	if (gConfig.isStatic(argv[1])) {
		os << argv[1] << " is static; change takes effect on restart" << endl;
	}
	if (!existing) {
		os << "defined new config " << argv[1] << " as \"" << val << "\"" << endl;
	} else {
		os << argv[1] << " changed from \"" << previousVal << "\" to \"" << val << "\"" << endl;
	}
	return SUCCESS;
}

/* TODO : add getFactoryCalibration to TranceiverRAD1
static CLIStatus trxfactory(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;

	signed val = gTRX.ARFCN(0)->getFactoryCalibration("sdrsn");
	if (val == 0 || val == 65535) {
		os << "Reading factory calibration not supported on this radio." << endl;
		return SUCCESS;
	}
	os << "Factory Information" << endl;
	os << "  SDR Serial Number = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("rfsn");
	os << "  RF Serial Number = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("band");
	os << "  UMTS.Radio.Band = ";
	if (val == 0) {
		os << "multi-band";
	} else {
		os << val;
	}
	os << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("rxgain");
	os << "  UMTS.Radio.RxGain = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("txgain");
	os << "  TRX.TxAttenOffset = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("freq");
	os << "  TRX.RadioFrequencyOffset = " << val << endl;

	return SUCCESS;
}
*/

/** Audit the current configuration. */
static CLIStatus audit(int argc, char **argv, ostream &os)
{
	ConfigurationKeyMap::iterator mp;
	stringstream ss;

	// value errors
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		if (!gConfig.isValidValue(mp->first, gConfig.getStr(mp->first))) {
			ss << mp->first << " \"" << gConfig.getStr(mp->first) << "\" (\""
			   << mp->second.getDefaultValue() << "\")" << endl;
		}
		mp++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| ERROR : Invalid Values [key current-value (default)]                |" << endl;
		os << "|   To use the default value again, execute: rmconfig key             |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	/* TODO : add getFactoryCalibration to TranceiverRAD1
	// factory calibration warnings
	signed sdrsn = gTRX.ARFCN(0)->getFactoryCalibration("sdrsn");
	if (sdrsn != 0 && sdrsn != 65535) {
		string factoryValue;
		string configValue;

		factoryValue = gConfig.mSchema["UMTS.Radio.Band"].getDefaultValue();
		configValue = gConfig.getStr("UMTS.Radio.Band");
		// only warn on band changes if the unit is not multi-band
		if (gTRX.ARFCN(0)->getFactoryCalibration("band") != 0 && configValue.compare(factoryValue) != 0) {
			ss << "UMTS.Radio.Band \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		factoryValue = gConfig.mSchema["UMTS.Radio.RxGain"].getDefaultValue();
		configValue = gConfig.getStr("UMTS.Radio.RxGain");
		if (configValue.compare(factoryValue) != 0) {
			ss << "UMTS.Radio.RxGain \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		factoryValue = gConfig.mSchema["TRX.TxAttenOffset"].getDefaultValue();
		configValue = gConfig.getStr("TRX.TxAttenOffset");
		if (configValue.compare(factoryValue) != 0) {
			ss << "TRX.TxAttenOffset \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		factoryValue = gConfig.mSchema["TRX.RadioFrequencyOffset"].getDefaultValue();
		configValue = gConfig.getStr("TRX.RadioFrequencyOffset");
		if (configValue.compare(factoryValue) != 0) {
			ss << "TRX.RadioFrequencyOffset \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		if (ss.str().length()) {
			os << "+---------------------------------------------------------------------+" << endl;
			os << "| WARNING : Factory Radio Calibration [key current-value (factory)]   |" << endl;
			os << "|   To use the factory value again, execute: rmconfig key             |" << endl;
			os << "+---------------------------------------------------------------------+" << endl;
			os << ss.str();
			os << endl;
			ss.str("");
		}
	}
	*/

	// cross check warnings
	vector<string> allWarnings;
	vector<string> warnings;
	vector<string>::iterator warning;
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		warnings = gConfig.crossCheck(mp->first);
		allWarnings.insert(allWarnings.end(), warnings.begin(), warnings.end());
		mp++;
	}
	sort(allWarnings.begin(), allWarnings.end());
	allWarnings.erase(unique(allWarnings.begin(), allWarnings.end()), allWarnings.end());
	warning = allWarnings.begin();
	while (warning != allWarnings.end()) {
		ss << *warning << endl;
		warning++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| WARNING : Cross-Check Values                                        |" << endl;
		os << "|   To quiet these warnings, follow the advice given.                 |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	// site-specific values
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		if (mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE) {
			if (gConfig.getStr(mp->first).compare(gConfig.mSchema[mp->first].getDefaultValue()) == 0) {
				ss << mp->first << " \"" << gConfig.mSchema[mp->first].getDefaultValue() << "\""
				   << endl;
			}
		}
		mp++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| WARNING : Site Values Which Are Still Default [key current-value]   |" << endl;
		os << "|   These should be set to fit your installation: config key value    |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	/* TODO : gNodeManager
	// unapplied values
	std::map<std::string, std::string> dirtyKeys = gNodeManager.getDirtyConfigurationKeysMap();
	std::map<std::string, std::string>::iterator dk = dirtyKeys.begin();
	while (dk != dirtyKeys.end()) {
		ss << dk->first << " \"" << gConfig.getStr(dk->first) << "\" (\"" << dk->second << "\")" << endl;
		dk++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| WARNING : Unapplied Changes [key desired-value (running-value)]     |" << endl;
		os << "|   To apply values, restart this daemon by executing: shutdown       |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}
	*/

	// non-default values
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		if (mp->second.getVisibility() != ConfigurationKey::CUSTOMERSITE) {
			if (gConfig.getStr(mp->first).compare(gConfig.mSchema[mp->first].getDefaultValue()) != 0) {
				ss << mp->first << " \"" << gConfig.getStr(mp->first) << "\" (\""
				   << mp->second.getDefaultValue() << "\")" << endl;
			}
		}
		mp++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| INFO : Non-Default Values [key current-value (default)]             |" << endl;
		os << "|   To use the default value again, execute: rmconfig key             |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	// unknown pairs
	ConfigurationRecordMap pairs = gConfig.getAllPairs();
	ConfigurationRecordMap::iterator mp2 = pairs.begin();
	while (mp2 != pairs.end()) {
		if (!gConfig.keyDefinedInSchema(mp2->first)) {
			// also kindly ignore SIM.Prog keys for now so the users don't kill their ability to program
			// SIMs
			string family = "SIM.Prog.";
			if (mp2->first.substr(0, family.size()) != family) {
				ss << mp2->first << " \"" << mp2->second.value() << "\"" << endl;
			}
		}
		mp2++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| INFO : Custom/Deprecated Key/Value Pairs [key current-value]        |" << endl;
		os << "|   To clean up any extraneous keys, execute: rmconfig key            |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	return SUCCESS;
}

/** Print or modify the global configuration table. */
CLIStatus _config(string mode, int argc, char **argv, ostream &os)
{
	// no args, just print
	if (argc == 1) {
		ConfigurationKeyMap::iterator mp = gConfig.mSchema.begin();
		while (mp != gConfig.mSchema.end()) {
			if (mode.compare("customer") == 0) {
				if (mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN) {
					ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
				}
			} else if (mode.compare("developer") == 0) {
				ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
			}
			mp++;
		}
		return SUCCESS;
	}

	// one arg
	if (argc == 2) {
		// matches exactly? print single key
		if (gConfig.keyDefinedInSchema(argv[1])) {
			ConfigurationKey::printKey(gConfig.mSchema[argv[1]], gConfig.getStr(argv[1]), os);
			ConfigurationKey::printDescription(gConfig.mSchema[argv[1]], os);
			os << endl;
			// ...otherwise print all similar keys
		} else {
			int foundCount = 0;
			ConfigurationKeyMap matches = gConfig.getSimilarKeys(argv[1]);
			ConfigurationKeyMap::iterator mp = matches.begin();
			while (mp != matches.end()) {
				if (mode.compare("customer") == 0) {
					if (mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN) {
						ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
						foundCount++;
					}
				} else if (mode.compare("developer") == 0) {
					ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
					foundCount++;
				}
				mp++;
			}
			if (!foundCount) {
				os << argv[1] << " - no keys matched";
				if (mode.compare("customer") == 0) {
					os << ", developer/factory keys can be accessed with \"devconfig.\"";
				} else if (mode.compare("developer") == 0) {
					os << ", custom keys can be accessed with \"rawconfig.\"";
				}
				os << endl;
			}
		}
		return SUCCESS;
	}

	// >1 args: set new value
	string val;
	for (int i = 2; i < argc; i++) {
		val.append(argv[i]);
		if (i != (argc - 1))
			val.append(" ");
	}
	if (!gConfig.keyDefinedInSchema(argv[1])) {
		os << argv[1]
		   << " is not a valid key, change failed. If you're trying to define a custom key/value pair (e.g. "
		      "the Log.Level.Filename.cpp pairs), use \"rawconfig.\""
		   << endl;
		return SUCCESS;
	}
	if (mode.compare("customer") == 0) {
		if (gConfig.mSchema[argv[1]].getVisibility() == ConfigurationKey::DEVELOPER) {
			os << argv[1]
			   << " should only be changed by developers. Use \"devconfig\" if you are ABSOLUTELY sure "
			      "this needs to be changed."
			   << endl;
			return SUCCESS;
		}
		if (gConfig.mSchema[argv[1]].getVisibility() == ConfigurationKey::FACTORY) {
			os << argv[1]
			   << " should only be set once by the factory. Use \"devconfig\" if you are ABSOLUTELY sure "
			      "this needs to be changed."
			   << endl;
			return SUCCESS;
		}
	}
	if (!gConfig.isValidValue(argv[1], val)) {
		os << argv[1] << " new value \"" << val << "\" is invalid, change failed.";
		if (mode.compare("developer") == 0) {
			os << " To override the configuration value checks, use \"rawconfig.\"";
		}
		os << endl;
		return SUCCESS;
	}

	string previousVal = gConfig.getStr(argv[1]);
	if (val.compare(previousVal) == 0) {
		os << argv[1] << " is already set to \"" << val << "\", nothing changed" << endl;
		return SUCCESS;
	}
	// TODO : removing of default values from DB disabled for now. Breaks webui.
	//	if (val.compare(gConfig.mSchema[argv[1]].getDefaultValue()) == 0) {
	//		if (!gConfig.remove(argv[1])) {
	//			os << argv[1] << " storing new value (default) failed" << endl;
	//			return SUCCESS;
	//		}
	//	} else {
	if (!gConfig.set(argv[1], val)) {
		os << "DB ERROR: " << argv[1] << " could not be updated" << endl;
		return FAILURE;
	}
	//	}
	// TODO : need to re-implement getARFCNsString() to provide info for UMTS
	// if (string(argv[1]).compare("UMTS.Radio.Band") == 0) {
	//	gConfig.mSchema["UMTS.Radio.C0"].updateValidValues(getARFCNsString(gConfig.getNum("UMTS.Radio.Band")));
	//}
	vector<string> warnings = gConfig.crossCheck(argv[1]);
	vector<string>::iterator warning = warnings.begin();
	while (warning != warnings.end()) {
		os << "WARNING: " << *warning << endl;
		warning++;
	}
	if (gConfig.isStatic(argv[1])) {
		os << argv[1] << " is static; change takes effect on restart" << endl;
	}
	os << argv[1] << " changed from \"" << previousVal << "\" to \"" << val << "\"" << endl;

	return SUCCESS;
}

/** Print or modify the global configuration table. Customer access. */
static CLIStatus config(int argc, char **argv, ostream &os) { return _config("customer", argc, argv, os); }

/** Print or modify the global configuration table. Developer/factory access. */
static CLIStatus devconfig(int argc, char **argv, ostream &os) { return _config("developer", argc, argv, os); }

/** Disable a configuration key. */
static CLIStatus unconfig(int argc, char **argv, ostream &os)
{
	if (argc != 2)
		return BAD_NUM_ARGS;

	if (!gConfig.defines(argv[1])) {
		os << argv[1] << " is not in the table" << endl;
		return BAD_VALUE;
	}

	if (gConfig.keyDefinedInSchema(argv[1]) && !gConfig.isValidValue(argv[1], "")) {
		os << argv[1] << " is not disableable" << endl;
		return BAD_VALUE;
	}

	if (!gConfig.set(argv[1], "")) {
		os << "DB ERROR: " << argv[1] << " could not be disabled" << endl;
		return FAILURE;
	}

	os << argv[1] << " disabled" << endl;

	return SUCCESS;
}

/** Set a configuration value back to default or remove from table if custom key. */
static CLIStatus rmconfig(int argc, char **argv, ostream &os)
{
	if (argc != 2)
		return BAD_NUM_ARGS;

	if (!gConfig.defines(argv[1])) {
		os << argv[1] << " is not in the table" << endl;
		return BAD_VALUE;
	}

	// TODO : removing of default values from DB disabled for now. Breaks webui.
	if (gConfig.keyDefinedInSchema(argv[1])) {
		if (!gConfig.set(argv[1], gConfig.mSchema[argv[1]].getDefaultValue())) {
			os << "DB ERROR: " << argv[1] << " could not be set back to the default value" << endl;
			return FAILURE;
		}

		os << argv[1] << " set back to its default value" << endl;
		vector<string> warnings = gConfig.crossCheck(argv[1]);
		vector<string>::iterator warning = warnings.begin();
		while (warning != warnings.end()) {
			os << "WARNING: " << *warning << endl;
			warning++;
		}
		if (gConfig.isStatic(argv[1])) {
			os << argv[1] << " is static; change takes effect on restart" << endl;
		}
		return SUCCESS;
	}

	if (!gConfig.remove(argv[1])) {
		os << "DB ERROR: " << argv[1] << " could not be removed from the configuration table" << endl;
		return FAILURE;
	}

	os << argv[1] << " removed from the configuration table" << endl;

	return SUCCESS;
}

/** Change the registration timers. */
static CLIStatus regperiod(int argc, char **argv, ostream &os)
{
	if (argc == 1) {
		os << "T3212 is " << gConfig.getNum("GSM.Timer.T3212") << " minutes" << endl;
		os << "SIP registration period is " << gConfig.getNum("SIP.RegistrationPeriod") << " minutes" << endl;
		return SUCCESS;
	}

	if (argc > 3)
		return BAD_NUM_ARGS;

	unsigned newT3212 = strtol(argv[1], NULL, 10);
	if (!gConfig.isValidValue("UMTS.Timer.T3212", argv[1])) {
		os << "valid T3212 range is 6..1530 minutes" << endl;
		return BAD_VALUE;
	}

	// By default, make SIP registration period 1.5x the GSM registration period.
	unsigned SIPRegPeriod = newT3212 * 1.5;
	char SIPRegPeriodStr[10];
	sprintf(SIPRegPeriodStr, "%u", SIPRegPeriod);
	if (argc == 3) {
		SIPRegPeriod = strtol(argv[2], NULL, 10);
		sprintf(SIPRegPeriodStr, "%s", argv[2]);
	}
	if (!gConfig.isValidValue("SIP.RegistrationPeriod", SIPRegPeriodStr)) {
		os << "valid SIP registration range is 6..2298 minutes" << endl;
		return BAD_VALUE;
	}

	// Set the values in the table and on the GSM beacon.
	gConfig.set("SIP.RegistrationPeriod", SIPRegPeriod);
	gConfig.set("UMTS.Timer.T3212", newT3212);
	// Done.
	return SUCCESS;
}

/** Print the list of alarms kept by the logger, i.e. the last LOG(ALARM) << <text> */
static CLIStatus alarms(int argc, char **argv, ostream &os)
{
	std::ostream_iterator<std::string> output(os, "\n");
	std::list<std::string> alarms = gGetLoggerAlarms();
	std::copy(alarms.begin(), alarms.end(), output);
	return SUCCESS;
}

/** Version string. */
static CLIStatus version(int argc, char **argv, ostream &os)
{
	if (argc != 1)
		return BAD_NUM_ARGS;
	os << gVersionString << endl;
	return SUCCESS;
}

/** Show start-up notices. */
static CLIStatus notices(int argc, char **argv, ostream &os)
{
	if (argc != 1)
		return BAD_NUM_ARGS;
	os << endl << gOpenWelcome << endl;
	return SUCCESS;
}

static CLIStatus page(int argc, char **argv, ostream &os)
{
	if (argc == 1) {
		gNodeB.pager().dump(os);
		return SUCCESS;
	}
	if (argc != 3)
		return BAD_NUM_ARGS;
	char *IMSI = argv[1];
	if (strlen(IMSI) > 15) {
		os << IMSI << " is not a valid IMSI" << endl;
		return BAD_VALUE;
	}
	Control::TransactionEntry dummy(gConfig.getStr("SIP.Proxy.SMS").c_str(), GSM::L3MobileIdentity(IMSI), NULL,
		GSM::L3CMServiceType::UndefinedType, GSM::L3CallingPartyBCDNumber("0"), GSM::Paging);
	gNodeB.pager().addID(GSM::L3MobileIdentity(IMSI), UMTS::DCCHType, dummy, 1000 * atoi(argv[2]));
	return SUCCESS;
}

static CLIStatus endcall(int argc, char **argv, ostream &os)
{
	if (argc != 2)
		return BAD_NUM_ARGS;
	unsigned transID = atoi(argv[1]);
	Control::TransactionEntry *target = gTransactionTable.find(transID);
	if (!target) {
		os << transID << " not found in table";
		return BAD_VALUE;
	}
	target->terminate();
	return SUCCESS;
}

static CLIStatus power(int argc, char **argv, ostream &os)
{
	//	os << "current downlink power " << gNodeB.powerManager().power() << " dB wrt full scale" << endl;
	os << "current attenuation bounds " << gConfig.getNum("UMTS.Radio.PowerManager.MinAttenDB") << " to "
	   << gConfig.getNum("UMTS.Radio.PowerManager.MaxAttenDB") << " dB" << endl;

	if (argc == 1)
		return SUCCESS;
	if (argc != 3)
		return BAD_NUM_ARGS;

	int min = atoi(argv[1]);
	int max = atoi(argv[2]);
	if (min > max) {
		os << "Min is larger than max" << endl;
		return BAD_VALUE;
	}

	if (!gConfig.isValidValue("UMTS.Radio.PowerManager.MinAttenDB", argv[1])) {
		os << "Invalid new value for min.  It must be in range (";
		os << gConfig.mSchema["UMTS.Radio.PowerManager.MinAttenDB"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("UMTS.Radio.PowerManager.MaxAttenDB", argv[2])) {
		os << "Invalid new value for max.  It must be in range (";
		os << gConfig.mSchema["UMTS.Radio.PowerManager.MaxAttenDB"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}

	gConfig.set("UMTS.Radio.PowerManager.MinAttenDB", argv[1]);
	gConfig.set("UMTS.Radio.PowerManager.MaxAttenDB", argv[2]);

	os << "new attenuation bounds " << gConfig.getNum("UMTS.Radio.PowerManager.MinAttenDB") << " to "
	   << gConfig.getNum("UMTS.Radio.PowerManager.MaxAttenDB") << " dB" << endl;

	return SUCCESS;
}

static CLIStatus rxgain(int argc, char **argv, ostream &os)
{
	os << "current RX gain is " << gConfig.getNum("UMTS.Radio.RxGain") << " dB" << endl;
	if (argc == 1)
		return SUCCESS;
	if (argc != 2)
		return BAD_NUM_ARGS;

	if (!gConfig.isValidValue("GSM.Radio.RxGain", argv[1])) {
		os << "Invalid new value for RX gain.  It must be in range (";
		os << gConfig.mSchema["GSM.Radio.RxGain"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}

	/* TODO : gTRX.ARFCN(0)->setRxGain
int newGain = gTRX.ARFCN(0)->setRxGain(atoi(argv[1]));
os << "new RX gain is " << newGain << " dB" << endl;

gConfig.set("UMTS.Radio.RxGain",newGain);
	*/

	return SUCCESS;
}

static CLIStatus txatten(int argc, char **argv, ostream &os)
{
	os << "current TX attenuation is " << gConfig.getNum("TRX.TxAttenOffset") << " dB" << endl;
	if (argc == 1)
		return SUCCESS;
	if (argc != 2)
		return BAD_NUM_ARGS;

	if (!gConfig.isValidValue("TRX.TxAttenOffset", argv[1])) {
		os << "Invalid new value for TX attenuation.  It must be in range (";
		os << gConfig.mSchema["TRX.TxAttenOffset"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}

	/* TODO : gTRX.ARFCN(0)->setTxAtten
int newAtten = gTRX.ARFCN(0)->setTxAtten(atoi(argv[1]));
os << "new TX attenuation is " << newAtten << " dB" << endl;

gConfig.set("TRX.TxAttenOffset",newAtten);
	*/

	return SUCCESS;
}

static CLIStatus temperature(int argc, char **argv, ostream &os)
{
	if (argc != 1)
		return BAD_NUM_ARGS;

	int temperature = gTRX.ARFCN(0)->getTemperature();

	os << "temperature is " << temperature << " C" << endl;

	return SUCCESS;
}

/*
// TODO : re-add support for noise command, right now it's not implemented in transceiver code
static CLIStatus noise(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;

	int noise = gTRX.ARFCN(0)->getNoiseLevel();
	os << "noise RSSI is -" << noise << " dB wrt full scale" << endl;
	os << "MS RSSI target is " << gConfig.getNum("UMTS.Radio.RSSITarget") << " dB wrt full scale" << endl;

	return SUCCESS;
}
*/

static CLIStatus freqcorr(int argc, char **argv, ostream &os)
{
	os << "current freq. offset is " << gConfig.getNum("TRX.RadioFrequencyOffset") << endl;
	if (argc == 1)
		return SUCCESS;
	if (argc != 2)
		return BAD_NUM_ARGS;

	if (!gConfig.isValidValue("TRX.RadioFrequencyOffset", argv[1])) {
		os << "Invalid new value for freq. offset  It must be in range (";
		os << gConfig.mSchema["TRX.RadioFrequencyOffset"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}

	int newOffset = gTRX.ARFCN(0)->setFreqOffset(atoi(argv[1]));
	os << "new freq. offset is " << newOffset << endl;

	gConfig.set("TRX.RadioFrequencyOffset", newOffset);

	return SUCCESS;
}

static CLIStatus memStat(int argc, char **argv, ostream &os)
{
	gMemStats.text(os);
	return SUCCESS;
}

static CLIStatus crashme(int argc, char **argv, ostream &os)
{
	char *nullp = 0x0;
	// we actually have to output this,
	// or the compiler will optimize it out
	os << *nullp;
	return FAILURE;
}

/* TODO : stats CLI
static CLIStatus stats(int argc, char** argv, ostream& os)
{

	char cmd[200];
	if (argc==2) {
		if (strcmp(argv[1],"clear")==0) {
				gReports.clear();
				os << "stats table (gReporting) cleared" << endl;
				return SUCCESS;
		}
		sprintf(cmd,"sqlite3 %s 'select name||\": \"||value||\" events over \"||((%lu-clearedtime)/60)||\"
minutes\" from reporting where name like \"%%%s%%\";'", gConfig.getStr("Control.Reporting.StatsTable").c_str(),
time(NULL), argv[1]);
	}
	else if (argc==1)
		sprintf(cmd,"sqlite3 %s 'select name||\": \"||value||\" events over \"||((%lu-clearedtime)/60)||\"
minutes\" from reporting;'", gConfig.getStr("Control.Reporting.StatsTable").c_str(), time(NULL)); else return
BAD_NUM_ARGS; FILE *result = popen(cmd,"r"); char *line = (char*)malloc(200); while (!feof(result)) { if (!fgets(line,
200, result)) break; os << line;
	}
	free(line);
	os << endl;
	pclose(result);
	return SUCCESS;
}
*/

//@} // CLI commands

void Parser::addCommands()
{
	addCommand("uptime", uptime, "-- show BTS uptime and BTS frame number.");
	addCommand("help", showHelp, "[command] -- list available commands or gets help on a specific command.");
	addCommand("shutdown", exit_function,
		"[wait] -- shut down or restart OpenBTS, either immediately, or waiting for existing calls to clear "
		"with a timeout in seconds");
	addCommand("sendsimple", sendsimple,
		"<IMSI> <src> -- send SMS to <IMSI> via SIP interface, addressed from <src>, after prompting.");
	// addCommand("load", printStats, "-- print the current activity loads.");
	addCommand("cellid", cellID,
		"[MCC MNC LAC CI] -- get/set location area identity (MCC, MNC, LAC) and cell ID (CI)");
	// addCommand("calls", calls, "-- print the transaction table");
	addCommand("rawconfig", rawconfig,
		"[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching "
		"a pattern, or set/change a configuration value");
	// addCommand("trxfactory", trxfactory, "-- print the radio's factory calibration and meta information");
	addCommand("audit", audit, "-- audit the current configuration for troubleshooting");
	addCommand("config", config,
		"[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching "
		"a pattern, or set/change a configuration value");
	addCommand("devconfig", devconfig,
		"[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching "
		"a pattern, or set/change a configuration value");
	addCommand("regperiod", regperiod, "[GSM] [SIP] -- get/set the registration period (GSM T3212), in MINUTES");
	addCommand("alarms", alarms, "-- show latest alarms");
	addCommand("version", version, "-- print the version string");
	addCommand("page", page, "print the paging table");
	addCommand("endcall", endcall, "[transID] -- ???");
	addCommand("power", power, "[minAtten maxAtten] -- report current attentuation or set min/max bounds");
	addCommand("rxgain", rxgain, "[newRxgain] -- get/set the RX gain in dB");
	// addCommand("noise", noise, "-- report receive noise level in RSSI dB");
	addCommand("temperature", temperature, "-- report temperature level in C");
	addCommand("unconfig", unconfig, "key -- disable a configuration key by setting an empty value");
	addCommand("txatten", txatten, "[newTxAtten] -- get/set the TX attenuation in dB");
	addCommand("freqcorr", freqcorr, "[newOffset] -- get/set the new radio frequency offset");
	addCommand("rmconfig", rmconfig,
		"key -- set a configuration value back to its default or remove a custom key/value pair");
	addCommand("notices", notices, "-- show startup copyright and legal notices");
	addCommand("sgsn", SGSN::sgsnCLI, "SGSN mode sub-command.  Type: sgsn help for more");
	addCommand("crashme", crashme, "force crash of OpenBTS for testing purposes");
	// addCommand("stats", stats,"[patt] OR clear -- print all, or selected, performance counters, OR clear all
	// counters");
	addCommand("rlctest", UMTS::rlcTest, "-- internal testing commands for UMTS");
	addCommand("rrctest", UMTS::rrcTest, "-- internal testing commands for UMTS");
	addCommand("memstat", memStat, "-- internal testing command: print memory use stats");
}

}; // namespace CommandLine
