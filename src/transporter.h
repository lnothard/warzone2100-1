/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** @file
 *  Functions for the display/functionality of the Transporter
 */

#ifndef __INCLUDED_SRC_TRANSPORTER_H__
#define __INCLUDED_SRC_TRANSPORTER_H__

#include "lib/widget/widget.h"

#define IDTRANS_FORM			9000	//The Transporter base form
#define IDTRANS_CONTENTFORM		9003	//The Transporter Contents form
#define IDTRANS_DROIDS			9006	//The Droid base form
#define IDTRANS_LAUNCH			9010	//The Transporter Launch button
#define	IDTRANS_CAPACITY		9500	//The Transporter capacity label

//defines how much space is on the Transporter
#define TRANSPORTER_CAPACITY		10

/// how much cargo capacity a droid takes up
int transporterSpaceRequired(const Droid* psDroid);

//initialises Transporter variables
void initTransporters();
// Refresh the transporter screen.
bool intRefreshTransporter();
/*Add the Transporter Interface*/
bool intAddTransporter(Droid* psSelected, bool offWorld);
/* Remove the Transporter widgets from the screen */
void intRemoveTrans(bool skipIntModeReset = false);
void intRemoveTransNoAnim(bool skipIntModeReset = false);
/* Process return codes from the Transporter Screen*/
void intProcessTransporter(UDWORD id);

/*Adds a droid to the transporter, removing it from the world*/
void transporterAddDroid(Droid* psTransporter, Droid* psDroidToAdd);
void transporterRemoveDroid(Droid* psTransport, Droid* psDroid, QUEUE_MODE mode);
/*check to see if the droid can fit on the Transporter - return true if fits*/
bool checkTransporterSpace(Droid const* psTransporter, Droid const* psAssigned, bool mayFlash = true);
/*calculates how much space is remaining on the transporter - allows droids to take
up different amount depending on their body size - currently all are set to one!*/
int calcRemainingCapacity(const Droid* psTransporter);

bool transporterIsEmpty(const Droid* psTransporter);

/*launches the defined transporter to the offworld map*/
bool launchTransporter(Droid* psTransporter);

/*checks how long the transporter has been travelling to see if it should
have arrived - returns true when there*/
bool updateTransporter(Droid* psTransporter);

void intUpdateTransCapacity(WIDGET* psWidget, W_CONTEXT* psContext);

/* Remove the Transporter Launch widget from the screen*/
void intRemoveTransporterLaunch();

//process the launch transporter button click
void processLaunchTransporter();

SDWORD bobTransporterHeight();

/*This is used to display the transporter button and capacity when at the home base ONLY*/
bool intAddTransporterLaunch(Droid* psDroid);

/* set current transporter (for script callbacks) */
void transporterSetScriptCurrent(Droid* psTransporter);

/* get current transporter (for script callbacks) */
Droid* transporterGetScriptCurrent();

/*called when a Transporter has arrived back at the LZ when sending droids to safety*/
void resetTransporter();

/* get time transporter launch button was pressed */
UDWORD transporterGetLaunchTime();

/*set the time for the Launch*/
void transporterSetLaunchTime(UDWORD time);

void flashMissionButton(UDWORD buttonID);
void stopMissionButtonFlash(UDWORD buttonID);
/*checks the order of the droid to see if its currently flying*/
bool transporterFlying(Droid* psTransporter);
//initialise the flag to indicate the first transporter has arrived - set in startMission()
void initFirstTransporterFlag();

#endif // __INCLUDED_SRC_TRANSPORTER_H__
