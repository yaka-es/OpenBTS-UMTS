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

// (pat) WARNING is stupidly defined in /usr/local/include/osipparser2/osip_const.h.
// This must be outside the #ifndef LOGGER_H to fix it as long as Logger.h included after the above file.
#ifdef WARNING
#undef WARNING
#endif

#ifndef LOGGER_H
#define LOGGER_H

#include <syslog.h>
#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <list>
#include <map>
#include <string>

#define _LOG(level) \
	Log(LOG_##level).get() << pthread_self() \
	<< timestr() << " " __FILE__  ":"  << __LINE__ << ":" << __FUNCTION__ << ": "

#define IS_LOG_LEVEL(wLevel) (gGetLoggingLevel(__FILE__)>=LOG_##wLevel)

#ifdef NDEBUG
#define LOG(wLevel) \
	if (LOG_##wLevel!=LOG_DEBUG && IS_LOG_LEVEL(wLevel)) _LOG(wLevel)
#else
#define LOG(wLevel) \
	if (IS_LOG_LEVEL(wLevel)) _LOG(wLevel)
#endif

// pat: And for your edification here are the 'levels' as defined in syslog.h:
// LOG_EMERG   0  system is unusable
// LOG_ALERT   1  action must be taken immediately
// LOG_CRIT    2  critical conditions
// LOG_ERR     3  error conditions
// LOG_WARNING 4  warning conditions
// LOG_NOTICE  5  normal, but significant, condition
// LOG_INFO    6  informational message
// LOG_DEBUG   7  debug-level message

// (pat) added - print out a var and its name.
// Use like this: int descriptive_name; LOG(INFO)<<LOGVAR(descriptive_name);
#define LOGVAR2(name,val) " " << name << "=" << (val)
#define LOGVAR(var) (" " #var "=") << var
#define LOGVARM(var) " " << &#var[1] << "=" << var		// Strip the first char ("m") off the var name when printing.
#define LOGVARP(var) (" " #var "=(") << var <<")"	// Put value in parens; used for classes.
#define LOGHEX(var) (" " #var "=0x") << hex << ((unsigned)var) << dec
#define LOGHEX2(name,val) " " << name << "=0x" << hex << ((unsigned)(val)) << dec
// These are kind of cheesy, but you can use for bitvector
#define LOGBV2(name,val) " " << name << "=(" << val<<" size:"<<val.size()<<")"
#define LOGBV(bv) LOGBV2(#bv,bv)
#define LOGVARRANGE(name,cur,lo,hi) " "<<name <<"=("<<(cur) << " range:"<<(lo) << " to "<<(hi) <<")"

#define LOGWATCH(stuff) if (gConfig.getNum("UMTS.Debug.Messages")) \
		std::cout << Utils::timestr().substr(4)<<" "<<stuff<<"\n";


#define OBJLOG(wLevel) \
	LOG(wLevel) << "obj: " << this << ' '

#define LOG_ASSERT(x) { if (!(x)) LOG(EMERG) << "assertion " #x " failed"; } assert(x);


#include "Threads.h"		// must be after defines above, if these files are to be allowed to use LOG()
#include "Utils.h"

/**
	A C++ stream-based thread-safe logger.
	Derived from Dr. Dobb's Sept. 2007 issue.
	Updated to use syslog.
	This object is NOT the global logger;
	every log record is an object of this class.
*/
class Log {

	public:

	protected:

	std::ostringstream mStream;		///< This is where we buffer up the log entry.
	int mPriority;					///< Priority of current report.
	bool mDummyInit;

	public:

	Log(int wPriority)
		:mPriority(wPriority), mDummyInit(false)
	{ }

	Log(const char* name, const char* level=NULL, int facility=LOG_USER);

	// Most of the work is in the destructor.
	/** The destructor actually generates the log entry. */
	~Log();

	std::ostringstream& get();
};
extern bool gLogToConsole;	// Pat added for easy debugging.



extern std::list<std::string> gGetLoggerAlarms();		///< Get a copy of the recent alarm list.


/**@ Global control and initialization of the logging system. */
//@{
/** Initialize the global logging system. */
extern void gLogInit(const std::string &name, const std::string &level, int facility = LOG_USER);
/** Get the logging level associated with a given file. */
extern int gGetLoggingLevel(const char *filename=NULL);
/** Allow early logging when still in constructors */
extern void gLogEarly(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
//@}


#endif

// vim: ts=4 sw=4
