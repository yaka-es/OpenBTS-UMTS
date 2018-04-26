/**@file GSM/SIP Call Control -- GSM 04.08, ISDN ITU-T Q.931, SIP IETF RFC-3261, RTP IETF RFC-3550. */

/*
 * OpenBTS provides an open source alternative to legacy telco protocols and
 * traditionally complex, proprietary hardware systems.
 *
 * Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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

/*
	Abbreviations:
	MTC -- Mobile Terminated Connect (someone calling the mobile)
	MOC -- Mobile Originated Connect (mobile calling out)
	MTD -- Mobile Terminated Disconnect (other party hangs up)
	MOD -- Mobile Originated Disconnect (mobile hangs up)
	E-MOC -- Emergency Mobile Originated Connect (mobile calling out)
*/

#include <CommonLibs/Logger.h>
#include <GSM/GSMCommon.h>
#include <GSM/GSML3CCMessages.h>
#include <GSM/GSML3MMMessages.h>
#include <GSM/GSML3RRMessages.h>
#include <Globals/Globals.h>
#include <SIP/SIPEngine.h>
#include <SIP/SIPInterface.h>
#include <SIP/SIPMessage.h>
#include <SIP/SIPUtility.h>
#include <UMTS/UMTSConfig.h>
#include <UMTS/UMTSLogicalChannel.h>

#include "CallControl.h"
#include "ControlCommon.h"
#include "MobilityManagement.h"
#include "SMSControl.h"
#include "TransactionTable.h"

#undef WARNING

using namespace std;
using namespace Control;

/**
	Return an even UDP port number for the RTP even/odd pair.
*/
unsigned allocateRTPPorts()
{
	// FIXME -- We need a real port allocator.
	const unsigned base = gConfig.getNum("RTP.Start");
	const unsigned range = gConfig.getNum("RTP.Range");
	const unsigned top = base + range;
	static Mutex lock;
	// Pick a random starting point.
	static unsigned port = base + 2 * (random() % (range / 2));
	lock.lock();
	unsigned retVal = port;
	port += 2;
	if (port >= top)
		port = base;
	lock.unlock();
	return retVal;
}

/**
	Force clearing on the GSM side.
	@param transaction The call transaction record.
	@param LCH The logical channel.
	@param cause The L3 abort cause.
*/
void forceGSMClearing(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, const GSM::L3Cause &cause)
{
	LOG(INFO) << "Q.931 state " << transaction->GSMState();
	// Already cleared?
	if (transaction->GSMState() == GSM::NullState)
		return;
	// Clearing not started?  Start it.
	if (!transaction->clearingGSM())
		LCH->send(GSM::L3Disconnect(transaction->L3TI(), cause));
	// Force the rest.
	LCH->send(GSM::L3ReleaseComplete(transaction->L3TI()));
	LCH->send(GSM::L3ChannelRelease());
	transaction->resetTimers();
	transaction->GSMState(GSM::NullState);
	LCH->send(GSM::RELEASE);
}

/**
	Force clearing on the SIP side.
	@param transaction The call transaction record.
*/
void forceSIPClearing(TransactionEntry *transaction)
{
	SIP::SIPState state = transaction->SIPState();
	LOG(INFO) << "SIP state " << state;
	if (state == SIP::Cleared)
		return;
	if (state != SIP::MODClearing) {
		// This also changes the SIP state to "clearing".
		transaction->MODSendBYE();
	} else {
		transaction->MODResendBYE();
	}
	transaction->MODWaitForOK();
}

/**
	Abort the call.  Does not remove the transaction from the table.
	@param transaction The call transaction record.
	@param LCH The logical channel.
	@param cause The L3 abort cause.
*/
void abortCall(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, const GSM::L3Cause &cause)
{
	LOG(INFO) << "cause: " << cause << ", transction: " << *transaction;
	if (LCH)
		forceGSMClearing(transaction, LCH, cause);
	forceSIPClearing(transaction);
}

/**
	Abort the call and remove the transaction.
	@param transaction The call transaction record.
	@param LCH The logical channel.
	@param cause The L3 abort cause.
*/
void abortAndRemoveCall(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, const GSM::L3Cause &cause)
{
	abortCall(transaction, LCH, cause);
	gTransactionTable->remove(transaction);
}

/**
	Process a message received from the phone during a call.
	This function processes all deviations from the "call connected" state.
	For now, we handle call clearing and politely reject everything else.
	@param transaction The transaction record for this call.
	@param LCH The logical channel for the transaction.
	@param message A pointer to the receiver message.
	@return true If the call has been cleared and the channel released.
*/
bool callManagementDispatchGSM(
	TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, const GSM::L3Message *message)
{
	LOG(DEBUG) << "from " << transaction->subscriber() << " message " << *message;

	// FIXME -- This dispatch section should be something more efficient with PD and MTI swtiches.

	// Actually check state before taking action.
	// if (transaction->SIPState()==SIP::Cleared) return true;
	// if (transaction->GSMState()==GSM::NullState) return true;

	// Call connection steps.

	// Connect Acknowledge
	if (dynamic_cast<const GSM::L3ConnectAcknowledge *>(message)) {
		LOG(INFO) << "GSM Connect Acknowledge " << *transaction;
		transaction->resetTimers();
		transaction->GSMState(GSM::Active);
		return false;
	}

	// Connect
	// GSM 04.08 5.2.2.5 and 5.2.2.6
	if (dynamic_cast<const GSM::L3Connect *>(message)) {
		LOG(INFO) << "GSM Connect " << *transaction;
		transaction->resetTimers();
		transaction->GSMState(GSM::Active);
		return false;
	}

	// Call Confirmed
	// GSM 04.08 5.2.2.3.2
	// "Call Confirmed" is the GSM MTC counterpart to "Call Proceeding"
	if (dynamic_cast<const GSM::L3CallConfirmed *>(message)) {
		LOG(INFO) << "GSM Call Confirmed " << *transaction;
		transaction->resetTimer("303");
		transaction->setTimer("301");
		transaction->GSMState(GSM::MTCConfirmed);
		return false;
	}

	// Alerting
	// GSM 04.08 5.2.2.3.2
	if (dynamic_cast<const GSM::L3Alerting *>(message)) {
		LOG(INFO) << "GSM Alerting " << *transaction;
		transaction->resetTimer("310");
		transaction->setTimer("301");
		transaction->GSMState(GSM::CallReceived);
		return false;
	}

	// Call clearing steps.
	// Good diagrams in GSM 04.08 7.3.4

	// FIXME -- We should be checking TI values against the transaction object.

	// Disconnect (1st step of MOD)
	// GSM 04.08 5.4.3.2
	if (dynamic_cast<const GSM::L3Disconnect *>(message)) {
		LOG(INFO) << "GSM Disconnect " << *transaction;
		transaction->resetTimers();
		LCH->send(GSM::L3Release(transaction->L3TI()));
		transaction->setTimer("308");
		transaction->GSMState(GSM::ReleaseRequest);
		// FIXME -- Maybe we need to send CANCEL.  See ticket #172.
		transaction->MODSendBYE();
		return false;
	}

	// Release (2nd step of MTD)
	if (dynamic_cast<const GSM::L3Release *>(message)) {
		LOG(INFO) << "GSM Release " << *transaction;
		transaction->resetTimers();
		LCH->send(GSM::L3ReleaseComplete(transaction->L3TI()));
		LCH->send(GSM::L3ChannelRelease());
		transaction->GSMState(GSM::NullState);
		transaction->MTDSendOK();
		return true;
	}

	// Release Complete (3nd step of MOD)
	// GSM 04.08 5.4.3.4
	if (dynamic_cast<const GSM::L3ReleaseComplete *>(message)) {
		LOG(INFO) << "GSM Release Complete " << *transaction;
		transaction->resetTimers();
		LCH->send(GSM::L3ChannelRelease());
		transaction->GSMState(GSM::NullState);
		transaction->MODWaitForOK();
		return true;
	}

	// IMSI Detach -- the phone is shutting off.
	if (const GSM::L3IMSIDetachIndication *detach = dynamic_cast<const GSM::L3IMSIDetachIndication *>(message)) {
		// The IMSI detach procedure will release the LCH.
		LOG(INFO) << "GSM IMSI Detach " << *transaction;
		IMSIDetachController(detach, LCH->DCCH());
		forceSIPClearing(transaction);
		return true;
	}

	// Start DTMF
	// Transalate to RFC-2967 or RFC-2833.
	if (const GSM::L3StartDTMF *startDTMF = dynamic_cast<const GSM::L3StartDTMF *>(message)) {
		char key = startDTMF->key().IA5();
		LOG(INFO) << "DMTF key=" << key << ' ' << *transaction;
		bool success = false;
		if (gConfig.defines("SIP.DTMF.RFC2833")) {
			bool s = transaction->startDTMF(key);
			if (!s)
				LOG(ERR) << "DTMF RFC-28333 failed.";
			success |= s;
		}
		if (gConfig.defines("SIP.DTMF.RFC2967")) {
			unsigned bcd = GSM::encodeBCDChar(key);
			bool s = transaction->sendINFOAndWaitForOK(bcd);
			if (!s)
				LOG(ERR) << "DTMF RFC-2967 failed.";
			success |= s;
		}
		if (success) {
			LCH->send(GSM::L3StartDTMFAcknowledge(transaction->L3TI(), startDTMF->key()));
		} else {
			LOG(CRIT) << "DTMF sending attempt failed; is any DTMF method defined?";
			// Cause 0x3f means "service or option not available".
			LCH->send(GSM::L3StartDTMFReject(transaction->L3TI(), 0x3f));
		}
		return false;
	}

	// Stop DTMF
	// RFC-2967 or RFC-2833
	if (dynamic_cast<const GSM::L3StopDTMF *>(message)) {
		transaction->stopDTMF();
		LCH->send(GSM::L3StopDTMFAcknowledge(transaction->L3TI()));
		return false;
	}

	// CM Service Request
	if (const GSM::L3CMServiceRequest *cmsrq = dynamic_cast<const GSM::L3CMServiceRequest *>(message)) {
		// SMS submission?  The rest will happen on the SACCH.
		if (cmsrq->serviceType().type() == GSM::L3CMServiceType::ShortMessage) {
			LOG(INFO) << "in call SMS submission on " << *LCH;
			InCallMOSMSStarter(transaction);
			LCH->send(GSM::L3CMServiceAccept());
			return false;
		}
		// For now, we are rejecting anything else.
		LOG(NOTICE) << "cannot accept additional CM Service Request from " << transaction->subscriber();
		// Cause 0x20 means "serivce not supported".
		LCH->send(GSM::L3CMServiceReject(0x20));
		return false;
	}

	// Stubs for unsupported features.
	// We need to answer the handset so it doesn't hang.

	// Hold
	if (dynamic_cast<const GSM::L3Hold *>(message)) {
		LOG(NOTICE) << "rejecting hold request from " << transaction->subscriber();
		// Default cause is 0x3f, option not available
		LCH->send(GSM::L3HoldReject(transaction->L3TI(), 0x3f));
		return false;
	}

	if (message) {
		LOG(NOTICE) << "no support for message " << *message << " from " << transaction->subscriber();
	} else {
		LOG(NOTICE) << "no support for unrecognized message from " << transaction->subscriber();
	}

	// If we got here, we're ignoring the message.
	return false;
}

/**
	Update vocoder data transfers in both directions.
	@param transaction The transaction object for this call.
	@param TCH The traffic channel for this call.
	@return True if anything was transferred.
*/
bool updateCallTraffic(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *TCH)
{
	bool activity = false;

	// Transfer in the downlink direction (RTP->GSM).
	// Blocking call.  On average returns 1 time per 20 ms.
	// Returns non-zero if anything really happened.
	// Make the rxFrame buffer big enough for G.711.
	unsigned char rxFrame[160];
	if (transaction->rxFrame(rxFrame)) {
		activity = true;
		TCH->sendTCH(rxFrame);
	}

	// Transfer in the uplink direction (GSM->RTP).
	// Flush FIFO to limit latency.
	unsigned maxQ = gConfig.getNum("GSM.MaxSpeechLatency");
	while (TCH->queueSize() > maxQ)
		delete[] TCH->recvTCH();
	if (unsigned char *txFrame = TCH->recvTCH()) {
		activity = true;
		// Send on RTP.
		transaction->txFrame(txFrame);
		delete[] txFrame;
	}

	// Return a flag so the caller will know if anything transferred.
	return activity;
}

/**
	Check GSM signalling.
	Can block for up to 52 GSM L1 frames (240 ms) because LCH::send is blocking.
	@param transaction The call's TransactionEntry.
	@param LCH The call's logical channel (TCH/FACCH or SDCCH).
	@return true If the call was cleared, but the transaction is still there.
*/
bool updateGSMSignalling(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, unsigned timeout = 0)
{
	if (transaction->GSMState() == GSM::NullState)
		return true;

	// Any Q.931 timer expired?
	if (transaction->anyTimerExpired()) {
		// Cause 0x66, "recover on timer expiry"
		abortCall(transaction, LCH, GSM::L3Cause(0x66));
		return true;
	}

	// Look for a control message from MS side.
	if (GSM::L3Frame *l3 = LCH->recv(timeout)) {
		// Check for lower-layer error.
		if (l3->primitive() == GSM::ERROR)
			return true;
		// Parse and dispatch.
		GSM::L3Message *l3msg = parseL3(*l3);
		delete l3;
		bool cleared = false;
		if (l3msg) {
			LOG(DEBUG) << "received " << *l3msg;
			cleared = callManagementDispatchGSM(transaction, LCH, l3msg);
			delete l3msg;
		}
		return cleared;
	}

	// If we are here, we have timed out, but assume the call is still running.
	return false;
}

/**
	Check SIP signalling.
	@param transaction The call's TransactionEntry.
	@param LCH The call's GSM logical channel (TCH/FACCH or SDCCH).
	@param GSMCleared True if the call is already cleared in the GSM domain.
	@return true If the call is cleared in the SIP domain.
*/
bool updateSIPSignalling(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, bool GSMCleared)
{

	// The main purpose of this code is to initiate disconnects from the SIP side.

	if (transaction->SIPState() == SIP::Cleared)
		return true;

	bool GSMClearedOrClearing = GSMCleared || transaction->clearingGSM();

	if (transaction->MTDCheckBYE() == SIP::MTDClearing) {
		LOG(DEBUG) << "got SIP BYE " << *transaction;
		if (!GSMClearedOrClearing) {
			// Initiate clearing in the GSM side.
			LCH->send(GSM::L3Disconnect(transaction->L3TI()));
			transaction->setTimer("305");
			transaction->GSMState(GSM::DisconnectIndication);
		} else {
			// GSM already cleared?
			// Ack the BYE and end the call.
			transaction->MTDSendOK();
		}
	}

	return transaction->SIPState() == SIP::Cleared;
}

/**
	Check SIP and GSM signalling.
	Can block for up to 52 GSM L1 frames (240 ms) because LCH::send is blocking.
	@param transaction The call's TransactionEntry.
	@param LCH The call's logical channel (TCH/FACCH or SDCCH).
	@return true If the call is cleared in both domains.
*/
bool updateSignalling(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH, unsigned timeout = 0)
{

	bool GSMCleared = (updateGSMSignalling(transaction, LCH, timeout));
	bool SIPCleared = updateSIPSignalling(transaction, LCH, GSMCleared);
	return GSMCleared && SIPCleared;
}

/**
	Poll for activity while in a call.
	Sleep if needed to prevent fast spinning.
	Will block for up to 250 ms.
	@param transaction The call's TransactionEntry.
	@param TCH The call's TCH+FACCH.
	@return true If the call was cleared.
*/
bool pollInCall(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *TCH)
{
	// See if the radio link disappeared.
	if (TCH->radioFailure()) {
		LOG(NOTICE) << "radio link failure, dropped call";
		forceSIPClearing(transaction);
		return true;
	}

	// Process pending SIP and GSM signalling.
	// If this returns true, it means the call is fully cleared.
	if (updateSignalling(transaction, TCH))
		return true;

	// Did an outside process request a termination?
	if (transaction->terminationRequested()) {
		// Cause 25 is "pre-emptive clearing".
		abortCall(transaction, TCH, 25);
		// Do the hard release to short-cut the timers.
		// If something else is requesting termination,
		// it's probably because we need the channel for
		// something else (like an emegency call) right away.
		// TCH->send(GSM::HARDRELEASE);
		return true;
	}

	// Transfer vocoder data.
	// If anything happened, then the call is still up.
	if (updateCallTraffic(transaction, TCH))
		return false;

	// If nothing happened, sleep so we don't burn up the CPU cycles.
	msleep(50);
	return false;
}

/**
	Pause for a given time while managing the connection.
	Returns on timeout or call clearing.
	Used for debugging to simulate ringing at terminating end.
	@param transaction The transaction record for the call.
	@param TCH The TCH+FACCH sed for this call.
	@param waitTime_ms The maximum time to wait, in ms.
	@return true If the call is cleared during the wait.
*/
bool waitInCall(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *TCH, unsigned waitTime_ms)
{
	Timeval targetTime(waitTime_ms);
	LOG(DEBUG);
	while (!targetTime.passed()) {
		if (pollInCall(transaction, TCH))
			return true;
	}
	return false;
}

/**
	This is the standard call manangement loop, regardless of the origination type.
	This function returns when the call is cleared and the channel is released.
	@param transaction The transaction record for this call, will be cleared on exit.
	@param TCH The TCH+FACCH for the call.
*/
void callManagementLoop(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *TCH)
{
	LOG(INFO) << " call connected " << *transaction;
	// poll everything until the call is cleared
	while (!pollInCall(transaction, TCH)) {
	}
	gTransactionTable->remove(transaction);
}

/**
	This function starts MOC on the SDCCH to the point of TCH assignment.
	@param req The CM Service Request that started all of this.
	@param LCH The logical used to initiate call setup.
*/
void Control::MOCStarter(const GSM::L3CMServiceRequest *req, UMTS::DTCHLogicalChannel *LCH)
{
	assert(LCH);
	assert(req);
	LOG(INFO) << *req;

	// If we got a TMSI, find the IMSI.
	// Note that this is a copy, not a reference.
	GSM::L3MobileIdentity mobileID = req->mobileID();
	resolveIMSI(mobileID, LCH);

	// FIXME -- At this point, verify the that subscriber has access to this service.
	// If the subscriber isn't authorized, send a CM Service Reject with
	// cause code, 0x41, "requested service option not subscribed",
	// followed by a Channel Release with cause code 0x6f, "unspecified".
	// Otherwise, proceed to the next section of code.
	// For now, we are assuming that the phone won't make a call if it didn't
	// get registered.

	// Let the phone know we're going ahead with the transaction.
	LOG(INFO) << "sending CMServiceAccept";
	LCH->send(GSM::L3CMServiceAccept());

	// Get the Setup message.
	// GSM 04.08 5.2.1.2
	GSM::L3Message *msg_setup = getMessage(LCH);
	const GSM::L3Setup *setup = dynamic_cast<const GSM::L3Setup *>(msg_setup);
	if (!setup) {
		if (msg_setup) {
			LOG(WARNING) << "Unexpected message " << *msg_setup;
			delete msg_setup;
		}
		throw UnexpectedMessage();
	}
	LOG(INFO) << *setup;
	// Pull out the L3 short transaction information now.
	// See GSM 04.07 11.2.3.1.3.
	// Set the high bit, since this TI came from the MS.
	unsigned L3TI = setup->TI() | 0x08;
	if (!setup->haveCalledPartyBCDNumber()) {
		// FIXME -- This is quick-and-dirty, not following GSM 04.08 5.
		LOG(WARNING) << "MOC setup with no number";
		// Cause 0x60 "Invalid mandatory information"
		LCH->send(GSM::L3ReleaseComplete(L3TI, 0x60));
		LCH->send(GSM::L3ChannelRelease());
		// The SIP side and transaction record don't exist yet.
		// So we're done.
		delete msg_setup;
		return;
	}

	LOG(DEBUG) << "SIP start engine";
	// Get the users sip_uri by pulling out the IMSI.
	// const char *IMSI = mobileID.digits();
	// Pull out Number user is trying to call and use as the sip_uri.
	const char *bcdDigits = setup->calledPartyBCDNumber().digits();

	// Create a transaction table entry so the TCH controller knows what to do later.
	// The transaction on the TCH will be a continuation of this one.
	TransactionEntry *transaction = new TransactionEntry(gConfig.getStr("SIP.Proxy.Speech").c_str(), mobileID, LCH,
		req->serviceType(), L3TI, setup->calledPartyBCDNumber());
	LOG(DEBUG) << "transaction: " << *transaction;
	gTransactionTable->add(transaction);

	// At this point, we have enough information start the SIP call setup.
	// We also have a SIP side and a transaction that will need to be
	// cleaned up on abort or clearing.

	// Now start a call by contacting asterisk.
	// Engine methods will return their current state.
	// The remote party will start ringing soon.
	LOG(DEBUG) << "starting SIP (INVITE) Calling " << bcdDigits;
	unsigned basePort = allocateRTPPorts();
	transaction->MOCSendINVITE(bcdDigits, gConfig.getStr("SIP.Local.IP").c_str(), basePort, SIP::RTPGSM610);
	LOG(DEBUG) << "transaction: " << *transaction;

	// Once we can start SIP call setup, send Call Proceeding.
	LOG(INFO) << "Sending Call Proceeding";
	LCH->send(GSM::L3CallProceeding(L3TI));
	transaction->GSMState(GSM::MOCProceeding);
	// Finally done with the Setup message.
	delete msg_setup;

	// The transaction is moving on to the MOCController.
	// If we need a TCH assignment, we do it here.
	LOG(DEBUG) << "transaction: " << *transaction;
#if 0
	// For very early assignment, we need a mode change.
	static const GSM::L3ChannelMode mode(GSM::L3ChannelMode::SpeechV1);
	LCH->send(GSM::L3ChannelModeModify(LCH->channelDescription(),mode));
	GSM::L3Message *msg_ack = getMessage(LCH);
	const GSM::L3ChannelModeModifyAcknowledge *ack =
	dynamic_cast<GSM::L3ChannelModeModifyAcknowledge*>(msg_ack);
	if (!ack) {
		if (msg_ack) {
			LOG(WARNING) << "Unexpected message " << *msg_ack;
			delete msg_ack;
		}
		throw UnexpectedMessage(transaction->ID());
	}
	// Cause 0x06 is "channel unacceptable"
	bool modeOK = (ack->mode()==mode);
	delete msg_ack;
	if (!modeOK) return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x06));
#endif
	MOCController(transaction, dynamic_cast<UMTS::DTCHLogicalChannel *>(LCH));
}

/**
	Continue MOC process on the TCH.
	@param transaction The call state and SIP interface.
	@param TCH The traffic channel to be used.
*/
void Control::MOCController(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *TCH)
{
	LOG(DEBUG) << "transaction: " << *transaction;
	unsigned L3TI = transaction->L3TI();
	assert(L3TI > 7);
	assert(TCH);

	// Look for RINGING or OK from the SIP side.
	// There's a T310 running on the phone now.
	// The phone will initiate clearing if it expires.
	// FIXME -- We should also have a SIP.Timer.B timeout on this end.
	while (transaction->GSMState() != GSM::CallReceived) {

		if (updateGSMSignalling(transaction, TCH))
			return;
		if (transaction->clearingGSM())
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));

		LOG(INFO) << "wait for Ringing or OK";
		SIP::SIPState state = transaction->MOCWaitForOK();
		LOG(DEBUG) << "SIP state=" << state;
		switch (state) {
		case SIP::Busy:
			LOG(INFO) << "SIP:Busy, abort";
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x11));
		case SIP::Fail:
			LOG(NOTICE) << "SIP:Fail, abort";
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));
		case SIP::Ringing:
			LOG(INFO) << "SIP:Ringing, send Alerting and move on";
			TCH->send(GSM::L3Alerting(L3TI));
			transaction->GSMState(GSM::CallReceived);
			break;
		case SIP::Active:
			LOG(DEBUG) << "SIP:Active, move on";
			transaction->GSMState(GSM::CallReceived);
			break;
		case SIP::Proceeding:
			LOG(DEBUG) << "SIP:Proceeding, send progress";
			TCH->send(GSM::L3Progress(L3TI));
			break;
		case SIP::Timeout:
			LOG(NOTICE) << "SIP:Timeout, reinvite";
			state = transaction->MOCResendINVITE();
			break;
		default:
			LOG(NOTICE) << "SIP unexpected state " << state;
			break;
		}
	}

	// There's a question here of what entity is generating the "patterns"
	// (ringing, busy signal, etc.) during call set-up.  For now, we're ignoring
	// that question and hoping the phone will make its own ringing pattern.

	// Wait for the SIP session to start.
	// There's a timer on the phone that will initiate clearing if it expires.
	LOG(INFO) << "wait for SIP OKAY";
	SIP::SIPState state = transaction->SIPState();
	while (state != SIP::Active) {

		LOG(DEBUG) << "wait for SIP session start";
		state = transaction->MOCWaitForOK();
		LOG(DEBUG) << "SIP state " << state;

		// check GSM state
		if (updateGSMSignalling(transaction, TCH))
			return;
		if (transaction->clearingGSM())
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));

		// parse out SIP state
		switch (state) {
		case SIP::Busy:
			// Should this be possible at this point?
			LOG(INFO) << "SIP:Busy, abort";
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x11));
		case SIP::Fail:
			LOG(INFO) << "SIP:Fail, abort";
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));
		case SIP::Proceeding:
			LOG(DEBUG) << "SIP:Proceeding, NOT sending progress";
			// TCH->send(GSM::L3Progress(L3TI));
			break;
		// For these cases, do nothing.
		case SIP::Timeout:
			// FIXME We should abort if this happens too often.
			// For now, we are relying on the phone, which may have bugs of its own.
		case SIP::Active:
		default:
			break;
		}
	}

	// Let the phone know the call is connected.
	LOG(INFO) << "sending Connect to handset";
	TCH->send(GSM::L3Connect(L3TI));
	transaction->setTimer("313");
	transaction->GSMState(GSM::ConnectIndication);

	// The call is open.
	transaction->MOCInitRTP();
	transaction->MOCSendACK();

	// FIXME -- We need to watch for a repeated OK in case the ACK got lost.

	// Get the Connect Acknowledge message.
	while (transaction->GSMState() != GSM::Active) {
		LOG(DEBUG) << "MOC Q.931 state=" << transaction->GSMState();
		if (updateGSMSignalling(transaction, TCH, T313ms))
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));
	}

	// At this point, everything is ready to run the call.
	callManagementLoop(transaction, TCH);

	// The radio link should have been cleared with the call.
	// So just return.
}

void Control::MTCStarter(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH)
{
	assert(LCH);
	LOG(INFO) << "MTC on " << LCH->type() << " transaction: " << *transaction;

	// Get transaction identifiers.
	// This transaction was created by the SIPInterface when it
	// processed the INVITE that started this call.
	unsigned L3TI = transaction->L3TI();
	assert(L3TI < 7);

	// GSM 04.08 5.2.2.1
	LOG(INFO) << "sending GSM Setup to call " << transaction->calling();
	LCH->send(GSM::L3Setup(L3TI, GSM::L3CallingPartyBCDNumber(transaction->calling())));
	transaction->setTimer("303");
	transaction->GSMState(GSM::CallPresent);

	// Wait for Call Confirmed message.
	LOG(DEBUG) << "wait for GSM Call Confirmed";
	while (transaction->GSMState() != GSM::MTCConfirmed) {
		if (transaction->MTCSendTrying() == SIP::Fail) {
			LOG(NOTICE) << "call failed on SIP side";
			LCH->send(GSM::RELEASE);
			// Cause 0x03 is "no route to destination"
			return abortAndRemoveCall(transaction, LCH, GSM::L3Cause(0x03));
		}
		// FIXME -- What's the proper timeout here?
		// It's the SIP TRYING timeout, whatever that is.
		if (updateGSMSignalling(transaction, LCH, 1000)) {
			LOG(INFO) << "Release from GSM side";
			LCH->send(GSM::RELEASE);
			return;
		}
		// Check for SIP cancel, too.
		if (transaction->MTCCheckForCancel() == SIP::Fail) {
			LOG(NOTICE) << "call cancelled or failed on SIP side";
			// Cause 0x15 is "rejected"
			return abortAndRemoveCall(transaction, LCH, GSM::L3Cause(0x15));
		}
	}

	// The transaction is moving to the MTCController.
	// Once this update happens, don't change the transaction object again in this function.
	LOG(DEBUG) << "transaction: " << *transaction;
#if 0
	if (veryEarly) {
		// For very early assignment, we need a mode change.
		static const GSM::L3ChannelMode mode(GSM::L3ChannelMode::SpeechV1);
		LCH->send(GSM::L3ChannelModeModify(LCH->channelDescription(),mode));
		GSM::L3Message* msg_ack = getMessage(LCH);
		const GSM::L3ChannelModeModifyAcknowledge *ack =
			dynamic_cast<GSM::L3ChannelModeModifyAcknowledge*>(msg_ack);
		if (!ack) {
			if (msg_ack) {
				LOG(WARNING) << "Unexpected message " << *msg_ack;
				delete msg_ack;
			}
			throw UnexpectedMessage(transaction->ID());
		}
		// Cause 0x06 is "channel unacceptable"
		bool modeOK = (ack->mode()==mode);
		delete msg_ack;
		if (!modeOK) return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x06));
	}
#endif
	MTCController(transaction, dynamic_cast<UMTS::DTCHLogicalChannel *>(LCH));
}

void Control::MTCController(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *TCH)
{
	// Early Assignment Mobile Terminated Call.
	// Transaction table in 04.08 7.3.3 figure 7.10a

	LOG(DEBUG) << "transaction: " << *transaction;
	unsigned L3TI = transaction->L3TI();
	assert(L3TI < 7);
	assert(TCH);

	// Get the alerting message.
	LOG(INFO) << "waiting for GSM Alerting and Connect";
	while (transaction->GSMState() != GSM::Active) {
		if (updateGSMSignalling(transaction, TCH, 1000))
			return;
		if (transaction->GSMState() == GSM::Active)
			break;
		if (transaction->GSMState() == GSM::CallReceived) {
			LOG(DEBUG) << "sending SIP Ringing";
			transaction->MTCSendRinging();
		}
		// Check for SIP cancel, too.
		if (transaction->MTCCheckForCancel() == SIP::Fail) {
			LOG(DEBUG) << "MTCCheckForCancel return Fail";
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));
		}
	}

	// FIXME -- We should also have a SIP.Timer.F timeout here.
	LOG(INFO) << "allocating port and sending SIP OKAY";
	unsigned RTPPorts = allocateRTPPorts();
	SIP::SIPState state = transaction->MTCSendOK(RTPPorts, SIP::RTPGSM610);
	while (state != SIP::Active) {
		LOG(DEBUG) << "wait for SIP OKAY-ACK";
		if (updateGSMSignalling(transaction, TCH))
			return;
		state = transaction->MTCWaitForACK();
		LOG(DEBUG) << "SIP call state " << state;
		switch (state) {
		case SIP::Active:
			break;
		case SIP::Fail:
			return abortAndRemoveCall(transaction, TCH, GSM::L3Cause(0x7F));
		case SIP::Timeout:
			state = transaction->MTCSendOK(RTPPorts, SIP::RTPGSM610);
			break;
		case SIP::Connecting:
			break;
		default:
			LOG(NOTICE) << "SIP unexpected state " << state;
			break;
		}
	}
	transaction->MTCInitRTP();

	// Send Connect Ack to make it all official.
	LOG(DEBUG) << "MTC send GSM Connect Acknowledge";
	TCH->send(GSM::L3ConnectAcknowledge(L3TI));

	// At this point, everything is ready to run for the call.
	// The radio link should have been cleared with the call.
	callManagementLoop(transaction, TCH);
}

void Control::TestCall(TransactionEntry *transaction, UMTS::DTCHLogicalChannel *LCH)
{
	assert(LCH);
	LOG(INFO) << LCH->type() << " transaction: " << *transaction;
	assert(transaction->L3TI() < 7);

	// Mark the call as active.
	transaction->GSMState(GSM::Active);

	// Create and open the control port.
	UDPSocket controlSocket(gConfig.getNum("TestCall.Port"));

	// If this is a FACCH, change the mode from signaling-only to speech.
#if 0
	if (LCH->type()==UMTS::DTCHType) {
		static const GSM::L3ChannelMode mode(GSM::L3ChannelMode::SpeechV1);
		LCH->send(GSM::L3ChannelModeModify(LCH->channelDescription(),mode));
		GSM::L3Message *msg_ack = getMessage(LCH);
		const GSM::L3ChannelModeModifyAcknowledge *ack =
			dynamic_cast<GSM::L3ChannelModeModifyAcknowledge*>(msg_ack);
		if (!ack) {
			if (msg_ack) {
				LOG(WARNING) << "Unexpected message " << *msg_ack;
				delete msg_ack;
			}
			controlSocket.close();
			throw UnexpectedMessage(transaction->ID());
		}
		// Cause 0x06 is "channel unacceptable"
		bool modeOK = (ack->mode()==mode);
		delete msg_ack;
		if (!modeOK) {
			controlSocket.close();
			return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x06));
		}
	}
#endif

	// FIXME -- Somehow, the RTP ports need to be attached to the transaction.

	// This loop will run or block until some outside entity writes a
	// channel release on the socket.

	LOG(INFO) << "entering test loop";
	while (true) {
		// Get the outgoing message from the test call port.
		char iBuf[MAX_UDP_LENGTH];
		int msgLen = controlSocket.read(iBuf);
		LOG(INFO) << "got " << msgLen << " bytes on UDP";
		// Send it to the handset.
		GSM::L3Frame query(iBuf, msgLen);
		LOG(INFO) << "sending " << query;
		LCH->send(query);
		// Wait for a response.
		// FIXME -- This should be a proper T3xxx value of some kind.
		GSM::L3Frame *resp = LCH->recv(30000);
		if (!resp) {
			LOG(NOTICE) << "read timeout";
			break;
		}
		if (resp->primitive() != GSM::DATA) {
			LOG(NOTICE) << "unexpected primitive " << resp->primitive();
			break;
		}
		LOG(INFO) << "received " << *resp;
		// Send response on the port.
		unsigned char oBuf[resp->size()];
		resp->pack(oBuf);
		controlSocket.writeBack((char *)oBuf);
		// Delete and close the loop.
		delete resp;
	}
	controlSocket.close();
	LOG(INFO) << "ending";
	LCH->send(GSM::L3ChannelRelease());
	LCH->send(GSM::RELEASE);
	gTransactionTable->remove(transaction);
}

void Control::initiateMTTransaction(
	Control::TransactionEntry *transaction, UMTS::ChannelTypeL3 chanType, unsigned pageTime)
{
	gTransactionTable->add(transaction);
	transaction->GSMState(GSM::Paging);
	gNodeB->pager().addID(transaction->subscriber(), chanType, *transaction, pageTime);
}
