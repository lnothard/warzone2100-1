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

/**
 * @file design.h
 */

#ifndef __INCLUDED_SRC_DESIGN_H__
#define __INCLUDED_SRC_DESIGN_H__

#include "lib/widget/widgbase.h"

/* Design screen ID's */
static constexpr auto IDDES_FORM = 5000; // The base form for the design screen
static constexpr auto IDDES_STATSFORM = 5001; // The design screen stats form
static constexpr auto IDDES_SYSTEMFORM = 5006; // The clickable form for the weapon/ecm/sensor
static constexpr auto IDDES_BODYFORM = 5007; // The clickable form for the body
static constexpr auto IDDES_PROPFORM = 5008; // The clickable form for the propulsion
static constexpr auto IDDES_3DVIEW = 5009; // The 3D view of the droid
static constexpr auto IDDES_BIN = 5011; // The bin button
static constexpr auto IDDES_NAMEBOX = 5013; // The Name box
static constexpr auto IDDES_POWERFORM = 5019; // The form for the power and points bars
static constexpr auto IDDES_TEMPLBASE = 5020; // The base form for the Template (left) form
static constexpr auto IDDES_RIGHTBASE = 5021; // The base form for the right form!
static constexpr auto IDDES_POWERBAR = 5023; // The power bar for the template

static constexpr auto IDDES_WEAPONS = 5024; // The weapon button for the Component form (right)
static constexpr auto IDDES_SYSTEMS = 5025; // The systems (sensor/ecm) button for the Component form
static constexpr auto IDDES_COMMAND = 5026; // The command button for the Component form

static constexpr auto IDDES_PARTFORM = 5027; // Part buttons form
static constexpr auto IDDES_WEAPONS_A = 5028; // The weapon TURRET_A button for the Component form (right)
static constexpr auto IDDES_WEAPONS_B = 5029; // The weapon TURRET_B button for the Component form (right)

static constexpr auto IDDES_STOREBUTTON = 5905; // Stored template button

/* Design screen bar graph IDs */
static constexpr auto IDDES_BODYARMOUR_K = 5100; // The body armour bar graph for kinetic weapons
static constexpr auto IDDES_BODYPOWER = 5101; // The body power plant graph
static constexpr auto IDDES_BODYWEIGHT = 5102; // The body weight
static constexpr auto IDDES_PROPROAD = 5105; // The propulsion road speed
static constexpr auto IDDES_PROPCOUNTRY = 5106; // The propulsion cross country speed
static constexpr auto IDDES_PROPWATER = 5107; // The propulsion water speed
static constexpr auto IDDES_PROPAIR = 5108; // The propulsion air speed
static constexpr auto IDDES_PROPWEIGHT = 5109; // The propulsion weight
static constexpr auto IDDES_SENSORRANGE = 5110; // The sensor range
static constexpr auto IDDES_SYSTEMSWEIGHT = 5112; // The systems weight
static constexpr auto IDDES_ECMPOWER = 5115; // The ecm power
static constexpr auto IDDES_WEAPRANGE = 5120; // The weapon range
static constexpr auto IDDES_WEAPDAMAGE = 5121; // The weapon damage
static constexpr auto IDDES_WEAPROF = 5122; // The weapon rate of fire
static constexpr auto IDDES_CONSTPOINTS = 5125; // The construction build points
//extras added AB 3/9/97
static constexpr auto IDDES_BODYPOINTS = 5127; // The body points bar graph
static constexpr auto IDDES_BODYARMOUR_H = 5128; // The body armour bar graph for heat weapons
static constexpr auto IDDES_REPAIRPOINTS = 5129; // The Repair points

/* Design screen bar graph labels */
static constexpr auto IDDES_BODYARMOURKLAB = 5200; // The body armour (kinetic) bar graph label
static constexpr auto IDDES_BODYPOWERLAB = 5201; // The body power plant graph label
static constexpr auto IDDES_BODYWEIGHTLAB = 5202; // The body weight graph label
static constexpr auto IDDES_PROPROADLAB = 5205; // The propulsion road speed label
static constexpr auto IDDES_PROPCOUNTRYLAB = 5206; // The propulsion cross country speed label
static constexpr auto IDDES_PROPWATERLAB = 5207; // The propulsion water speed label
static constexpr auto IDDES_PROPAIRLAB = 5208; // The propulsion air speed label
static constexpr auto IDDES_PROPWEIGHTLAB = 5209; // The propulsion weight label
static constexpr auto IDDES_SENSORRANGELAB = 5210; // The sensor range label
static constexpr auto IDDES_SYSTEMSWEIGHTLAB = 5212; // The systems weight label
static constexpr auto IDDES_ECMPOWERLAB = 5215; // The ecm power label
static constexpr auto IDDES_WEAPRANGELAB = 5220; // The weapon range label
static constexpr auto IDDES_WEAPDAMAGELAB = 5221; // The weapon damage label
static constexpr auto IDDES_WEAPROFLAB = 5222; // The weapon rate of fire label
static constexpr auto IDDES_CONSTPOINTSLAB = 5225; // The construction build points label
//extras added AB 3/9/97
//static constexpr auto IDDES_BODYPOINTSLAB = 5227; // The body points label
static constexpr auto IDDES_BODYARMOURHLAB = 5228; // The body armour (heat) bar graph label

static constexpr auto IDDES_TEMPPOWERLAB = 5229; // The template's Power req label
static constexpr auto IDDES_TEMPBODYLAB = 5230; // The template's Body Points label

static constexpr auto IDDES_REPAIRPTLAB = 5231; // The Repair Points label
static constexpr auto IDDES_REPAIRWGTLAB = 5232; // The Repair Weigth label

/* Design screen buttons */
static constexpr auto IDDES_TEMPLSTART = 5300; // The first design template button
static constexpr auto IDDES_TEMPLEND = 5339; // The last design template button
static constexpr auto IDDES_BARSTART = 5400;
static constexpr auto IDDES_BAREND = 5499;
static constexpr auto IDDES_COMPSTART = 5500000; // The first component button
static constexpr auto IDDES_COMPEND = 5565535; // The last component button
static constexpr auto IDDES_EXTRASYSSTART = 5700; // The first extra system button
static constexpr auto IDDES_EXTRASYSEND = 5899; // The last extra system button

static constexpr auto IDDES_SYSTEMBUTTON = 5900; // System button
static constexpr auto IDDES_BODYBUTTON = 5901; // Body button
static constexpr auto IDDES_PROPBUTTON = 5902; // Propulsion button
static constexpr auto IDDES_WPABUTTON = 5903; // WeaponA button
static constexpr auto IDDES_WPBBUTTON = 5904; // WeaponB button

bool intAddDesign(bool bShowCentreScreen);
void intRemoveDesign();
void intProcessDesign(UDWORD id);
void intRunDesign();

const char* GetDefaultTemplateName(DroidTemplate* psTemplate);

bool intValidTemplate(DroidTemplate* psTempl, const char* newName, bool complain, unsigned player);

#endif // __INCLUDED_SRC_DESIGN_H__
