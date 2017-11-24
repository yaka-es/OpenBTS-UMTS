/**@file Declarations for PhysicalStatus and related classes. */

/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 *
 * Copyright 2010 Kestrel Signal Processing, Inc.
 * Copyright 2011, 2014 Range Networks, Inc.
 *
 * This software is distributed under the terms of the GNU Affero General
 * Public License version 3. See the COPYING and NOTICE files in the main
 * directory for licensing information.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 */

#ifndef PHYSICALSTATUS_H
#define PHYSICALSTATUS_H

#include <map>

#include <CommonLibs/Threads.h>
#include <CommonLibs/Timeval.h>

struct sqlite3;

namespace GSM {

class L3MeasurementResults;
class LogicalChannel;

/**
	A table for tracking the state of channels.
*/
class PhysicalStatus {

private:
	Mutex mLock;  ///< to reduce the load on the filesystem locking
	sqlite3 *mDB; ///< database connection

public:
	/**
		Create a physical status reporting table.
		@param path Path fto sqlite3 database file.
	*/
	PhysicalStatus(const char *wPath);

	~PhysicalStatus();

	/**
		Add reporting information associated with a channel to the table.
		@param chan The channel to report.
		@param measResults The measurement report.
		@return The result of the SQLite query: true for the query being executed successfully, false otherwise.
	*/
	bool setPhysical(const LogicalChannel *chan, const L3MeasurementResults &measResults);

	/**
		Dump the physical status table to the output stream.
		@param os The output stream to dump the channel information to.
	*/
	//	void dump(std::ostream& os) const;

private:
	/**
		Create entry in table. This is for the initial creation.
		@param chan The channel to create an entry for.
		@return The result of the SQLite query: true for the query being executed successfully, false otherwise.
	*/
	bool createEntry(const LogicalChannel *chan);
};

} // namespace GSM

#endif
