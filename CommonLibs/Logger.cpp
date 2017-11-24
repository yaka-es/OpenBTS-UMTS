/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 *
 * Copyright 2009, 2010 Free Software Foundation, Inc.
 * Copyright 2010 Kestrel Signal Processing, Inc.
 * Copyright 2011-2014 Range Networks, Inc.
 *
 * This software is distributed under the terms of the GNU Affero General
 * Public License version 3. See the COPYING and NOTICE files in the main
 * directory for licensing information.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 */

#include <stdarg.h>
#include <string.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "Configuration.h"
#include "Logger.h"
#include "Threads.h" // pat added

using namespace std;

/**@ The global alarms table. */
//@{
static Mutex alarms_mutex;
static list<string> alarms_list;
static void addAlarm(const string &s);
//@}

// (pat) If Log messages are printed before the classes in this module are inited
// (which happens when static classes have constructors that do work)
// the OpenBTS just crashes.
// Prevent that by setting sLoggerInited to true when this module is inited.
static bool sLoggerInited = 0;
static struct CheckLoggerInitStatus {
	CheckLoggerInitStatus() { sLoggerInited = 1; }
} sCheckloggerInitStatus;

/** Names of the logging levels. */
static const char *level_names[] = {"EMERG", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG"};
static int num_levels = 8;
static Mutex log_mutex;
bool gLogToConsole = 0;
FILE *gLogFile = NULL;

int levelStringToInt(const string &name)
{
	// Reverse search, since the numerically larger levels are more common.
	for (int i = num_levels - 1; i >= 0; i--) {
		if (name == level_names[i])
			return i;
	}

	// Common substitutions.
	if (name == "INFORMATION")
		return 6;
	if (name == "WARN")
		return 4;
	if (name == "ERROR")
		return 3;
	if (name == "CRITICAL")
		return 2;
	if (name == "EMERGENCY")
		return 0;

	// Unknown level.
	return -1;
}

/** Given a string, return the corresponding level name. */
int lookupLevel(const string &key)
{
	string val = gConfig.getStr(key);
	int level = levelStringToInt(val);

	if (level == -1) {
		string defaultLevel = gConfig.mSchema["Log.Level"].getDefaultValue();
		level = levelStringToInt(defaultLevel);
		_LOG(CRIT) << "undefined logging level (" << key << " = \"" << val << "\") defaulting to \""
			   << defaultLevel
			   << ".\" Valid levels are: EMERG, ALERT, CRIT, ERR, WARNING, NOTICE, INFO or DEBUG";
		gConfig.set(key, defaultLevel);
	}

	return level;
}

int getLoggingLevel(const char *filename)
{
	// Default level?
	if (!filename)
		return lookupLevel("Log.Level");

	// This can afford to be inefficient since it is not called that often.
	const string keyName = string("Log.Level.") + string(filename);
	if (gConfig.defines(keyName))
		return lookupLevel(keyName);
	return lookupLevel("Log.Level");
}

int gGetLoggingLevel(const char *filename)
{
	// This is called a lot and needs to be efficient.

	static Mutex sLogCacheLock;
	static map<uint64_t, int> sLogCache;
	static unsigned sCacheCount;
	static const unsigned sCacheRefreshCount = 1000;

	if (filename == NULL)
		return gGetLoggingLevel("");

	HashString hs(filename);
	uint64_t key = hs.hash();

	sLogCacheLock.lock();
	// Time for a cache flush?
	if (sCacheCount > sCacheRefreshCount) {
		sLogCache.clear();
		sCacheCount = 0;
	}
	// Is it cached already?
	map<uint64_t, int>::const_iterator where = sLogCache.find(key);
	sCacheCount++;
	if (where != sLogCache.end()) {
		int retVal = where->second;
		sLogCacheLock.unlock();
		return retVal;
	}
	// Look it up in the config table and cache it.
	// FIXME: Figure out why unlock and lock below fix the config table deadlock.
	// (pat) Probably because getLoggingLevel may call LOG recursively via lookupLevel().
	sLogCacheLock.unlock();
	int level = getLoggingLevel(filename);
	sLogCacheLock.lock();
	sLogCache.insert(pair<uint64_t, int>(key, level));
	sLogCacheLock.unlock();
	return level;
}

// copies the alarm list and returns it. list supposed to be small.
list<string> gGetLoggerAlarms()
{
	alarms_mutex.lock();
	list<string> ret;
	// excuse the "complexity", but to use std::copy with a list you need
	// an insert_iterator - copy technically overwrites, doesn't insert.
	insert_iterator<list<string>> ii(ret, ret.begin());
	copy(alarms_list.begin(), alarms_list.end(), ii);
	alarms_mutex.unlock();
	return ret;
}

/** Add an alarm to the alarm list. */
static void addAlarm(const string &s)
{
	alarms_mutex.lock();

	alarms_list.push_back(s);

	unsigned maxAlarms = gConfig.getNum("Log.Alarms.Max");

	while (alarms_list.size() > maxAlarms)
		alarms_list.pop_front();

	alarms_mutex.unlock();
}

Log::~Log()
{
	if (mDummyInit)
		return;
	// Anything at or above LOG_CRIT is an "alarm".
	// Save alarms in the local list and echo them to stderr.
	if (mPriority <= LOG_CRIT) {
		if (sLoggerInited)
			addAlarm(mStream.str().c_str());
		cerr << mStream.str() << endl;
	}
	// Current logging level was already checked by the macro.
	// So just log.
	syslog(mPriority, "%s", mStream.str().c_str());
	// pat added for easy debugging.
	if (gLogToConsole || gLogFile) {
		int mlen = mStream.str().size();
		int neednl = (mlen == 0 || mStream.str()[mlen - 1] != '\n');
		log_mutex.lock();
		if (gLogToConsole) {
			// The COUT() macro prevents messages from stomping each other but adds uninteresting thread
			// numbers, so just use std::cout.
			std::cout << mStream.str();
			if (neednl)
				std::cout << "\n";
		}
		if (gLogFile) {
			fputs(mStream.str().c_str(), gLogFile);
			if (neednl) {
				fputc('\n', gLogFile);
			}
			fflush(gLogFile);
		}
		log_mutex.unlock();
	}
}

Log::Log(const char *name, const char *level, int facility)
{
	mDummyInit = true;
	gLogInit(name, level, facility);
}

ostringstream &Log::get()
{
	assert(mPriority < num_levels);
	mStream << level_names[mPriority] << ' ';
	return mStream;
}

void gLogInit(const std::string &name, const std::string &level, int facility)
{
	// Set the level if one has been specified.
	if (!level.empty()) {
		gConfig.set("Log.Level", level);
	}

	// Pat added, tired of the syslog facility.
	// Both the transceiver and OpenBTS use this same facility, but only OpenBTS/OpenBTS-UMTS may use this log file:
	string logfile = gConfig.getStr("Log.File");

	bool this_app_is_openbts = (strncmp(gCmdName, "OpenBTS", 7) == 0);

	if ((gLogFile == 0) && !logfile.empty() && this_app_is_openbts) {
		const char *fn = logfile.c_str();

		if (fn && *fn && (strlen(fn) > 3)) { // strlen because a garbage char is getting in sometimes.
			gLogFile = fopen(fn, "w");   // New log file each time we start.

			if (gLogFile) {
				time_t now;
				time(&now);
				fprintf(gLogFile, "Starting at %s\n", ctime(&now));
				fflush(gLogFile);
				std::cout << "Logging to file: " << fn << "\n";
			}
		}
	}

	// Open the log connection.
	openlog(name.c_str(), 0, facility);
}

void gLogEarly(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsyslog(level | LOG_USER, fmt, args);
	va_end(args);
}
