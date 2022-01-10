/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004 Eidos Interactive
	Copyright (C) 2005-2020 Warzone 2100 Project

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
 * @file design.cpp
 * Functions for the design screen
 */

#include <algorithm>
#include <cstring>
#include <utility>

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/bitimage.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/ivis_opengl/piemode.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/widget/widget.h"
#include "lib/widget/bar.h"
#include "lib/widget/button.h"
#include "lib/gamelib/gtime.h"

#include "loop.h"
#include "map.h"
#include "objects.h"
#include "objmem.h"
#include "display3d.h"
#include "structure.h"
#include "research.h"
#include "hci.h"
#include "stats.h"
#include "power.h"
#include "order.h"
#include "projectile.h"
#include "intimage.h"
#include "intdisplay.h"
#include "design.h"
#include "component.h"
#include "main.h"
#include "display.h"
#include "cmddroid.h"
#include "mission.h"
#include "template.h"
#include "multiplay.h"
#include "qtscript.h"
#include "animation.h"

// Max number of stats the design screen can cope with.
static constexpr auto MAX_DESIGN_COMPONENTS = 40;
static constexpr auto MAX_SYSTEM_COMPONENTS = 65535;

/* Max values for the design bar graphs */

/// Maximum body points for a template
static constexpr auto DBAR_TEMPLATEMAXPOINTS = 8400;

/// Maximum power points for a template
static constexpr auto DBAR_TEMPLATEMAXPOWER = 1000;

/* The maximum number of characters on the component buttons */
static constexpr auto DES_COMPBUTMAXCHAR = 5;

/* Which type of system is displayed on the design screen */
enum DES_SYSMODE
{
	IDES_SENSOR,
	// The sensor clickable is displayed
	IDES_ECM,
	// The ECM clickable is displayed
	IDES_CONSTRUCT,
	// The Constructor clickable is displayed
	IDES_REPAIR,
	// The Repair clickable is displayed
	IDES_WEAPON,
	// The Weapon clickable is displayed
	IDES_COMMAND,
	// The command droid clickable is displayed
	IDES_NOSYSTEM,
	// No system clickable has been displayed
};

static DES_SYSMODE desSysMode;

/* The major component tabs on the design screen */
static constexpr auto IDES_MAINTAB = 0;
static constexpr auto IDES_EXTRATAB = 1;

/* Which component type is being selected on the design screen */
//added IDES_TURRET_A,IDES_TURRET_B,changing the name of IDES_TURRET might break exist codes
enum DES_COMPMODE
{
	IDES_SYSTEM,
	// The main system for the droid (sensor, ECM, constructor)
	IDES_TURRET,
	// The weapon for the droid
	IDES_BODY,
	// The droid body
	IDES_PROPULSION,
	// The propulsion system
	IDES_NOCOMPONENT,
	// No system has been selected
	IDES_TURRET_A,
	// The 2nd turret
	IDES_TURRET_B,
	// The 3rd turret
};

static DES_COMPMODE desCompMode;

/* Which type of propulsion is being selected */
enum DES_PROPMODE
{
	IDES_GROUND,
	// Ground propulsion (wheeled, tracked, etc).
	IDES_AIR,
	// Air propulsion
	IDES_NOPROPULSION,
	// No propulsion has been selected
};

static DES_PROPMODE desPropMode;


static constexpr auto STRING_BUFFER_SIZE = (32 * MAX_STR_LENGTH);
char StringBuffer[STRING_BUFFER_SIZE];

/* Design screen positions */
//the top left y value for all forms on the design screen
static const auto DESIGN_Y = (59 + D_H);

static constexpr auto DES_TABBUTGAP = 2;
static constexpr auto DES_TABBUTWIDTH = 60;
static constexpr auto DES_TABBUTHEIGHT = 46;

static constexpr auto DES_LEFTFORMX = RET_X;
static const auto DES_LEFTFORMY = DESIGN_Y;
static constexpr auto DES_LEFTFORMWIDTH = RET_FORMWIDTH;
static constexpr auto DES_LEFTFORMHEIGHT = 273;
static constexpr auto DES_LEFTFORMBUTX = 2;
static constexpr auto DES_LEFTFORMBUTY = 2;

static constexpr auto DES_CENTERFORMWIDTH = 315;
static constexpr auto DES_CENTERFORMHEIGHT = 262;
static const auto DES_CENTERFORMX = POW_X;
static const auto DES_CENTERFORMY = DESIGN_Y;

static constexpr auto DES_3DVIEWX = 8;
static constexpr auto DES_3DVIEWY = 25;
static constexpr auto DES_3DVIEWWIDTH = 236;
static constexpr auto DES_3DVIEWHEIGHT = 192;

static const auto DES_STATSFORMX = POW_X;
static const auto DES_STATSFORMY = (DES_CENTERFORMY + DES_CENTERFORMHEIGHT + 3);
static constexpr auto DES_STATSFORMWIDTH = DES_CENTERFORMWIDTH;
static constexpr auto DES_STATSFORMHEIGHT = 100;

static constexpr auto DES_PARTFORMX = DES_3DVIEWX + DES_3DVIEWWIDTH + 2;
static constexpr auto DES_PARTFORMY = DES_3DVIEWY;
static constexpr auto DES_PARTFORMHEIGHT = DES_3DVIEWHEIGHT;

static constexpr auto DES_POWERFORMX = DES_3DVIEWX;
static constexpr auto DES_POWERFORMY = (DES_3DVIEWY + DES_3DVIEWHEIGHT + 2);
static constexpr auto DES_POWERFORMWIDTH = (DES_CENTERFORMWIDTH - 2*DES_POWERFORMX);
static constexpr auto DES_POWERFORMHEIGHT = 40;

static constexpr auto DES_RIGHTFORMWIDTH = (RET_FORMWIDTH + 20);
static constexpr auto DES_RIGHTFORMHEIGHT = DES_LEFTFORMHEIGHT;
static constexpr auto DES_RIGHTFORMBUTX = 2;
static constexpr auto DES_RIGHTFORMBUTY = 2;

static constexpr auto DES_BARFORMX = 6;
static constexpr auto DES_BARFORMY = 6;
static constexpr auto DES_BARFORMWIDTH = 300;
static constexpr auto DES_BARFORMHEIGHT = 85;

static constexpr auto DES_NAMEBOXX = DES_3DVIEWX;
static constexpr auto DES_NAMEBOXY = 6;
static constexpr auto DES_NAMEBOXWIDTH = DES_CENTERFORMWIDTH - 2*DES_NAMEBOXX;
static constexpr auto DES_NAMEBOXHEIGHT = 14;

/* The central boxes on the design screen */
static constexpr auto DES_COMPBUTWIDTH = 150;
static constexpr auto DES_COMPBUTHEIGHT = 85;

static constexpr auto DES_POWERX = 1;
static constexpr auto DES_POWERY = 6;
static constexpr auto DES_POWERSEPARATIONX = 4;
static constexpr auto DES_POWERSEPARATIONY = 2;

static constexpr auto DES_PARTSEPARATIONX = 6;
static constexpr auto DES_PARTSEPARATIONY = 6;

/* Positions of stuff on the clickable boxes (Design screen) */
static constexpr auto DES_CLICKBARX = 154;
static constexpr auto DES_CLICKBARY = 7;
static constexpr auto DES_CLICKBARWIDTH = 140;
static constexpr auto DES_CLICKBARHEIGHT = 11;
static constexpr auto DES_CLICKGAP = 9;
static constexpr auto DES_CLICKBARNAMEX = 126;
static constexpr auto DES_CLICKBARNAMEWIDTH = 20;
static constexpr auto DES_CLICKBARNAMEHEIGHT = 19;

//0xcc
static constexpr auto DES_CLICKBARMAJORRED = 255;
//0
static constexpr auto DES_CLICKBARMAJORGREEN = 235;
//0
static constexpr auto DES_CLICKBARMAJORBLUE = 19;
static constexpr auto DES_CLICKBARMINORRED = 0x55;
static constexpr auto DES_CLICKBARMINORGREEN = 0;
static constexpr auto DES_CLICKBARMINORBLUE = 0;

static constexpr auto DES_WEAPONBUTTON_X = 26;
static constexpr auto DES_SYSTEMBUTTON_X = 68;
static constexpr auto DES_SYSTEMBUTTON_Y = 10;

// Stat bar y positions.
static constexpr auto DES_STATBAR_Y1 = (DES_CLICKBARY);
static constexpr auto DES_STATBAR_Y2 = (DES_CLICKBARY+DES_CLICKBARHEIGHT + DES_CLICKGAP);
static constexpr auto DES_STATBAR_Y3 = (DES_CLICKBARY+(DES_CLICKBARHEIGHT + DES_CLICKGAP)*2);
static constexpr auto DES_STATBAR_Y4 = (DES_CLICKBARY+(DES_CLICKBARHEIGHT + DES_CLICKGAP)*3);

/* default droid design template */
static DroidTemplate sDefaultDesignTemplate;

static void desSetupDesignTemplates();
static void setDesignPauseState();
static void resetDesignPauseState();
static bool intAddTemplateButtons(ListTabWidget* templList, DroidTemplate* psSelected);
static void intDisplayDesignForm(WIDGET* psWidget, unsigned xOffset, unsigned yOffset);

typedef std::function<bool(std::function<bool(ComponentStats&, size_t)>)> ComponentIterator;

/* Set the current mode of the design screen, and display the appropriate component lists */
static void intSetDesignMode(DES_COMPMODE newCompMode, bool forceRefresh = false);
/* Set all the design bar graphs from a design template */
static void intSetDesignStats(DroidTemplate* psTemplate);
/* Set up the system clickable form of the design screen given a set of stats */
static bool intSetSystemForm(ComponentStats* psStats);
/* Set up the propulsion clickable form of the design screen given a set of stats */
static bool intSetPropulsionForm(PropulsionStats* psStats);
/* Add the component tab form to the design screen */
static ListTabWidget* intAddComponentForm();
/* Add the template tab form to the design screen */
static bool intAddTemplateForm(DroidTemplate* psSelected);
/* Add the system buttons (weapons, command droid, etc) to the design screen */
static bool intAddSystemButtons(DES_COMPMODE mode);
/* Add the component buttons to the main tab of the system or component form */
static bool intAddComponentButtons(ListTabWidget* compList, const ComponentIterator& iterator, unsigned compID, bool bWeapon);
static uint32_t intCalcSpeed(TYPE_OF_TERRAIN type, PropulsionStats* psProp);
static uint32_t calculatePropulsionWeight(ComponentStats& propulsionStats);
/* Add the component buttons to the main tab of the component form */
static bool intAddExtraSystemButtons(ListTabWidget* compList, unsigned sensorIndex, unsigned ecmIndex,
                                     unsigned constIndex, unsigned repairIndex, unsigned brainIndex);
/* Set the bar graphs for the system clickable */
static void intSetSystemStats(ComponentStats* psStats);
/* Set the shadow bar graphs for the system clickable */
static void intSetSystemShadowStats(ComponentStats* psStats);
/* Set the bar graphs for the sensor stats */
static void intSetSensorStats(SensorStats* psStats);
/* Set the shadow bar graphs for the sensor stats */
static void intSetSensorShadowStats(SensorStats* psStats);
/* Set the bar graphs for the ECM stats */
static void intSetECMStats(EcmStats* psStats);
/* Set the shadow bar graphs for the ECM stats */
static void intSetECMShadowStats(EcmStats* psStats);
/* Set the bar graphs for the Repair stats */
static void intSetRepairStats(RepairStats* psStats);
/* Set the shadow bar graphs for the Repair stats */
static void intSetRepairShadowStats(RepairStats* psStats);
/* Set the bar graphs for the Constructor stats */
static void intSetConstructStats(ConstructStats* psStats);
/* Set the shadow bar graphs for the Constructor stats */
static void intSetConstructShadowStats(ConstructStats* psStats);
/* Set the bar graphs for the Weapon stats */
static void intSetWeaponStats(WeaponStats* psStats);
/* Set the shadow bar graphs for the weapon stats */
static void intSetWeaponShadowStats(WeaponStats* psStats);
/* Set the bar graphs for the Body stats */
static void intSetBodyStats(BodyStats* psStats);
/* Set the shadow bar graphs for the Body stats */
static void intSetBodyShadowStats(BodyStats* psStats);
/* Set the bar graphs for the Propulsion stats */
static void intSetPropulsionStats(PropulsionStats* psStats);
/* Set the shadow bar graphs for the Propulsion stats */
static void intSetPropulsionShadowStats(PropulsionStats* psStats);
/* Sets the Design Power Bar for a given Template */
static void intSetDesignPower(DroidTemplate* psTemplate);
/* Sets the Power shadow Bar for the current Template with new stat*/
static void intSetTemplatePowerShadowStats(ComponentStats* psStats);
/* Sets the Body Points Bar for a given Template */
static void intSetBodyPoints(DroidTemplate* psTemplate);
/* Sets the Body Points shadow Bar for the current Template with new stat*/
static void intSetTemplateBodyShadowStats(ComponentStats* psStats);
/* set flashing flag for button */
static void intSetButtonFlash(unsigned id, bool bFlash);
/*Function to set the shadow bars for all the stats when the mouse is over
the Template buttons*/
static void runTemplateShadowStats(unsigned id);

static bool intCheckValidWeaponForProp(DroidTemplate* psTemplate);

static bool checkTemplateIsVtol(const DroidTemplate* psTemplate);

/* save the current Template if valid. Return true if stored */
static bool saveTemplate();

static void desCreateDefaultTemplate();

static void setTemplateStat(DroidTemplate* psTemplate, ComponentStats* psStats);

/**
 * Updates the status of the stored template toggle button.
 *
 * @param isStored If the template is stored or not.
 */
static void updateStoreButton(bool isStored);

/* The current name of the design */
static char aCurrName[MAX_STR_LENGTH];

/* Store a list of component stats pointers for the design screen */
extern unsigned maxComponent;
extern unsigned numComponent;
extern ComponentStats** apsComponentList;
extern unsigned maxExtraSys;
extern unsigned numExtraSys;
extern ComponentStats** apsExtraSysList;

/* The button id of the component that is in the design */
static unsigned desCompID;

static unsigned droidTemplID;

/* The current design being edited on the design screen */
static DroidTemplate sCurrDesign;
static DroidTemplate sShadowDesign;

static void intDisplayStatForm(WIDGET* psWidget, unsigned xOffset, unsigned yOffset);
static void intDisplayViewForm(WIDGET* psWidget, unsigned xOffset, unsigned yOffset);

class DesignStatsBar : public W_BARGRAPH
{
public:
	explicit DesignStatsBar(W_BARINIT* init): W_BARGRAPH(init)
	{
	}

	static std::shared_ptr<DesignStatsBar> makeLessIsBetter(W_BARINIT* init)
	{
		auto widget = std::make_shared<DesignStatsBar>(init);
		widget->lessIsBetter = true;
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		auto x0 = xOffset + x() + PADDING;
		auto y0 = yOffset + y() + PADDING;

		/* indent to allow text value */
		auto iX = x0 + maxValueTextWidth;
		auto iY = y0 + (iV_GetImageHeight(IntImages, IMAGE_DES_STATSCURR) - iV_GetTextLineSize(font_regular)) / 2 -
			iV_GetTextAboveBase(font_regular);

		//draw current value section
		int barWidth = width() - maxValueTextWidth - 2 * PADDING;
		auto filledWidth = std::min<int>((int)majorSize * barWidth / 100, barWidth);
		iV_DrawImageRepeatX(IntImages, IMAGE_DES_STATSCURR, iX, y0, filledWidth, defaultProjectionMatrix(), true);

		valueText.setText(astringf("%.*f", precision, majorValue / (float)denominator), font_regular);
		valueText.render(x0, iY, WZCOL_TEXT_BRIGHT);

		if (minorValue == 0)
		{
			return;
		}

		//draw the comparison value - only if not zero
		updateAnimation();
		filledWidth = std::min<int>(static_cast<int>(minorAnimation.getCurrent() * barWidth / 100), barWidth);
		iV_DrawImage(IntImages, IMAGE_DES_STATSCOMP, iX + filledWidth, y0 - 1);

		auto delta = minorValue - majorValue;
		if (delta != 0)
		{
			deltaText.setText(astringf("%+.*f", precision, delta / (float)denominator), font_small);
			auto xDeltaText = xOffset + x() + width() - iV_GetTextWidth(deltaText.getText().c_str(), font_small) -
				PADDING;
			deltaText.renderOutlined(xDeltaText, iY - 1, (delta < 0) == lessIsBetter ? WZCOL_LGREEN : WZCOL_LRED,
			                         {0, 0, 0, 192});
		}
	}

	void updateAnimation()
	{
		if (minorSize != minorAnimation.getFinalData())
		{
			minorAnimation
				.setInitialData(minorAnimation.getCurrent())
				.setFinalData(minorSize)
				.start();
		}

		minorAnimation.update();
	}

	WzText valueText;
	WzText deltaText;
	uint32_t maxValueTextWidth = iV_GetTextWidth("00000", font_regular);
	Animation<float> minorAnimation = Animation<float>(
          realTime, EASING_FUNCTION::EASE_IN, 200).setInitialData(0).setFinalData(0);
	const uint32_t PADDING = 3;
	bool lessIsBetter = false;
};

class DesignPowerBar : public DesignStatsBar
{
public:
	explicit DesignPowerBar(W_BARINIT* init): DesignStatsBar(init)
	{
	}

	static std::shared_ptr<DesignPowerBar> makeLessIsBetter(W_BARINIT* init)
	{
		auto widget = std::make_shared<DesignPowerBar>(init);
		widget->lessIsBetter = true;
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		auto x0 = xOffset + x();
		auto y0 = yOffset + y();
		iV_DrawImage(IntImages, IMAGE_DES_POWERBAR_LEFT, x0, y0);
		iV_DrawImage(IntImages, IMAGE_DES_POWERBAR_RIGHT,
		             x0 + width() - iV_GetImageWidth(IntImages, IMAGE_DES_POWERBAR_RIGHT), y0);
		DesignStatsBar::display(xOffset, yOffset);
	}
};

static ComponentIterator componentIterator(ComponentStats* psStats, unsigned size, const UBYTE* aAvailable,
                                           unsigned numEntries)
{
	return [=](const std::function<bool(ComponentStats&, size_t index)>& callback)
	{
		for (unsigned i = 0; i < numEntries; ++i)
		{
			auto psCurrStats = (ComponentStats*)(((UBYTE*)psStats) + size * i);

			/* Skip unavailable entries and non-design ones*/
			if (!(aAvailable[i] == AVAILABLE || (includeRedundantDesigns && aAvailable[i] == REDUNDANT)) || !psCurrStats
				->designable)
			{
				continue;
			}

			if (!callback(*psCurrStats, i))
			{
				return false;
			}
		}

		return true;
	};
}

static ComponentIterator bodyIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asBodyStats, sizeof(*asBodyStats), apCompLists[selectedPlayer][COMPONENT_TYPE::BODY], numBodyStats);
}

static ComponentIterator weaponIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asWeaponStats, sizeof(*asWeaponStats), apCompLists[selectedPlayer][COMPONENT_TYPE::WEAPON],
	                         numWeaponStats);
}

static ComponentIterator propulsionIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asPropulsionStats, sizeof(*asPropulsionStats),
	                         apCompLists[selectedPlayer][COMPONENT_TYPE::PROPULSION], numPropulsionStats);
}

static ComponentIterator sensorIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asSensorStats, sizeof(SensorStats), apCompLists[selectedPlayer][COMPONENT_TYPE::SENSOR],
                           numSensorStats);
}

static ComponentIterator ecmIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asECMStats, sizeof(EcmStats), apCompLists[selectedPlayer][COMPONENT_TYPE::ECM], numECMStats);
}

static ComponentIterator constructorIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asConstructStats, sizeof(ConstructStats), apCompLists[selectedPlayer][COMPONENT_TYPE::CONSTRUCT],
                           numConstructStats);
}

static ComponentIterator repairIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asRepairStats, sizeof(RepairStats), apCompLists[selectedPlayer][COMPONENT_TYPE::REPAIRUNIT],
                           numRepairStats);
}

static ComponentIterator brainIterator()
{
	ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);
	return componentIterator(asBrainStats, sizeof(CommanderStats), apCompLists[selectedPlayer][COMPONENT_TYPE::BRAIN], numBrainStats);
}

static ComponentIterator concatIterators(const std::vector<ComponentIterator>& iterators)
{
	return [=](const std::function<bool(ComponentStats&, size_t index)>& callback)
	{
		for (const auto& iterator : iterators)
		{
			if (!iterator(callback))
			{
				return false;
			}
		}

		return true;
	};
}

static ComponentIterator extraSystemIterator()
{
	return concatIterators({sensorIterator(), ecmIterator(), constructorIterator(), repairIterator(), brainIterator()});
}

static uint32_t findMax(const ComponentIterator& componentIterator,
                       std::function<uint32_t(ComponentStats&)> value)
{
	uint32_t max = 0;

	componentIterator([&](ComponentStats& bodyStats, size_t index)
	{
		max = std::max(max, value(bodyStats));
		return true;
	});

	return max;
}

static uint32_t findMaxWeight(const ComponentIterator& componentIterator)
{
	return findMax(componentIterator,
                 [](ComponentStats& stats)
                 { return stats.weight; });
}

static uint32_t findMaxPropulsionSpeed(TYPE_OF_TERRAIN terrainType)
{
	return findMax(
		propulsionIterator(),
		[=](ComponentStats& stats) { return intCalcSpeed(terrainType, (PropulsionStats*)&stats); }
	);
}

static uint32_t findMaxWeaponAttribute(const std::function<uint32_t(WeaponStats*, int)>& attributeGetter)
{
	return findMax(
		weaponIterator(),
		[=](ComponentStats& stats) { return attributeGetter((WeaponStats*)&stats, selectedPlayer); }
	);
}

static uint32_t getDesignMaxBodyArmour(WEAPON_CLASS weaponClass)
{
	return findMax(
		bodyIterator(),
		[=](ComponentStats& stats) { return bodyArmour((BodyStats*)&stats, selectedPlayer, weaponClass); }
	);
}

static uint32_t getDesignMaxEngineOutput()
{
	return findMax(
		bodyIterator(),
		[=](ComponentStats& stats) { return bodyPower((BodyStats*)&stats, selectedPlayer); }
	);
}

static uint32_t calcShadowBodyPoints(ComponentStats& psStats)
{
	auto designCopy = sCurrDesign;
	setTemplateStat(&designCopy, &psStats);
	return calcTemplateBody(&designCopy, selectedPlayer);
}

static uint32_t calcShadowPower(ComponentStats& psStats)
{
	auto designCopy = sCurrDesign;
	setTemplateStat(&designCopy, &psStats);
	return calcTemplatePower(&designCopy);
}

static uint32_t getDesignMaxSensorRange()
{
	return findMax(
		sensorIterator(),
		[=](ComponentStats& stats) { return sensorRange((SensorStats*)&stats, selectedPlayer); }
	);
}

static uint32_t getDesignMaxEcmRange()
{
	return findMax(
		ecmIterator(),
		[=](ComponentStats& stats) { return ecmRange((EcmStats*)&stats, selectedPlayer); }
	);
}

static uint32_t getDesignMaxBuildPoints()
{
	return findMax(
		constructorIterator(),
		[=](ComponentStats& stats) { return constructorPoints((ConstructStats*)&stats, selectedPlayer); }
	);
}

/* Add the design widgets to the widget screen */
bool intAddDesign(bool bShowCentreScreen)
{
	W_FORMINIT sFormInit;
	W_LABINIT sLabInit;
	W_EDBINIT sEdInit;
	W_BUTINIT sButInit;
	W_BARINIT sBarInit;

	ASSERT_OR_RETURN(false, !(bMultiPlayer && NetPlay.players[selectedPlayer].isSpectator),
	                 "Spectators can't open design mode");
	ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "selectedPlayer: %" PRIu32 "", selectedPlayer);

	desSetupDesignTemplates();

	//set which states are to be paused while design screen is up
	setDesignPauseState();

	auto const& parent = psWScreen->psForm;

	/* Add the main design form */
	auto desForm = std::make_shared<IntFormAnimated>(false);
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
	desForm->id = IDDES_FORM;
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
	parent->attach(desForm);
	desForm->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->setGeometry(DES_CENTERFORMX, DES_CENTERFORMY, DES_CENTERFORMWIDTH, DES_CENTERFORMHEIGHT);
			}));

	/* add the edit name box */
	sEdInit.formID = IDDES_FORM;
	sEdInit.id = IDDES_NAMEBOX;
	sEdInit.x = DES_NAMEBOXX;
	sEdInit.y = DES_NAMEBOXY;
	sEdInit.width = DES_NAMEBOXWIDTH;
	sEdInit.height = DES_NAMEBOXHEIGHT;
	sEdInit.pText = _("New Vehicle");
	sEdInit.pBoxDisplay = intDisplayEditBox;
	if (!widgAddEditBox(psWScreen, &sEdInit))
	{
		return false;
	}

	/* Initialise the current design */
	sDefaultDesignTemplate.type = DROID_ANY;
	sCurrDesign = sDefaultDesignTemplate;
	sCurrDesign.isStored = false;
	sstrcpy(aCurrName, _("New Vehicle"));
	sCurrDesign.name = WzString::fromUtf8(aCurrName);

	/* Add the design templates form */
	if (!intAddTemplateForm(nullptr))
	// Was psCurrTemplate instead of NULL, but psCurrTemplate was always NULL. Deleted psCurrTemplate, but leaving this here, in case intAddTemplateForm(NULL) does something useful.
	{
		return false;
	}

	/* Add the 3D View form */
	sFormInit.formID = IDDES_FORM;
	sFormInit.id = IDDES_3DVIEW;
	sFormInit.style = WFORM_PLAIN;
	sFormInit.x = DES_3DVIEWX;
	sFormInit.y = DES_3DVIEWY;
	sFormInit.width = DES_3DVIEWWIDTH;
	sFormInit.height = DES_3DVIEWHEIGHT;
	sFormInit.pDisplay = intDisplayViewForm;
	if (!widgAddForm(psWScreen, &sFormInit))
	{
		return false;
	}

	/* Add the part button form */
	sFormInit.formID = IDDES_FORM;
	sFormInit.id = IDDES_PARTFORM;
	sFormInit.style = WFORM_PLAIN;
	sFormInit.x = DES_PARTFORMX;
	sFormInit.y = DES_PARTFORMY;
	sFormInit.width = (UWORD)(iV_GetImageWidth(IntImages, IMAGE_DES_TURRET) +
		2 * DES_PARTSEPARATIONX);
	sFormInit.height = DES_PARTFORMHEIGHT;
	sFormInit.pDisplay = intDisplayDesignForm;
	if (!widgAddForm(psWScreen, &sFormInit))
	{
		return false;
	}

	// add the body part button
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_BODYBUTTON;
	sButInit.x = DES_PARTSEPARATIONX;
	sButInit.y = DES_PARTSEPARATIONY;
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_BODY);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_BODY);
	sButInit.pTip = _("Vehicle Body");
	sButInit.pDisplay = intDisplayButtonFlash;
	sButInit.UserData = PACKDWORD_TRI(1, IMAGE_DES_BODYH, IMAGE_DES_BODY);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	// add the propulsion part button
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_PROPBUTTON;
	sButInit.x = DES_PARTSEPARATIONX;
	sButInit.y = (UWORD)(iV_GetImageHeight(IntImages, IMAGE_DES_PROPULSION) +
		2 * DES_PARTSEPARATIONY);
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_PROPULSION);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_PROPULSION);
	sButInit.pTip = _("Vehicle Propulsion");
	sButInit.pDisplay = intDisplayButtonFlash;
	sButInit.UserData = PACKDWORD_TRI(1, IMAGE_DES_PROPULSIONH, IMAGE_DES_PROPULSION);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	// add the turret part button
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_SYSTEMBUTTON;
	sButInit.x = DES_PARTSEPARATIONX;
	sButInit.y = (UWORD)(iV_GetImageHeight(IntImages, IMAGE_DES_PROPULSION) +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODY) +
		3 * DES_PARTSEPARATIONY);
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_TURRET);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_TURRET);
	sButInit.pTip = _("Vehicle Turret");
	sButInit.pDisplay = intDisplayButtonFlash;
	sButInit.UserData = PACKDWORD_TRI(1, IMAGE_DES_TURRETH, IMAGE_DES_TURRET);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	// add the turret_a button
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_WPABUTTON;
	sButInit.x = DES_PARTSEPARATIONX;
	// use BODY height for now
	sButInit.y = (UWORD)(iV_GetImageHeight(IntImages, IMAGE_DES_PROPULSION) +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODY) +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODY) +
		4 * DES_PARTSEPARATIONY);
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_TURRET);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_TURRET);
	sButInit.pTip = _("Vehicle Turret");
	sButInit.pDisplay = intDisplayButtonFlash;
	sButInit.UserData = PACKDWORD_TRI(1, IMAGE_DES_TURRETH, IMAGE_DES_TURRET);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	// add the turret_b button
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_WPBBUTTON;
	sButInit.x = DES_PARTSEPARATIONX;
	//use body height for now
	sButInit.y = (UWORD)(iV_GetImageHeight(IntImages, IMAGE_DES_PROPULSION) +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODY) +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODY) +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODY) +
		5 * DES_PARTSEPARATIONY);
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_TURRET);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_TURRET);
	sButInit.pTip = _("Vehicle Turret");
	sButInit.pDisplay = intDisplayButtonFlash;
	sButInit.UserData = PACKDWORD_TRI(1, IMAGE_DES_TURRETH, IMAGE_DES_TURRET);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	/* add the delete button */
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_BIN;
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_BIN);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_BIN);
	sButInit.x = DES_PARTSEPARATIONX;
	sButInit.y = (UWORD)(DES_PARTFORMHEIGHT - sButInit.height - DES_PARTSEPARATIONY);
	sButInit.pTip = _("Delete Design");
	sButInit.pDisplay = intDisplayButtonHilight;
	sButInit.UserData = PACKDWORD_TRI(0, IMAGE_DES_BINH, IMAGE_DES_BIN);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	// Add the store template button
	sButInit.formID = IDDES_PARTFORM;
	sButInit.id = IDDES_STOREBUTTON;
	sButInit.style = WBUT_PLAIN;
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_SAVE);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_SAVE);
	sButInit.x = DES_PARTSEPARATIONX;
	sButInit.y = DES_PARTFORMHEIGHT - 2 * sButInit.height - 2 * DES_PARTSEPARATIONY;
	sButInit.pTip = _("Store Template");
	sButInit.FontID = font_regular;
	sButInit.pDisplay = intDisplayButtonHilight;
	sButInit.UserData = PACKDWORD_TRI(0, IMAGE_DES_SAVEH, IMAGE_DES_SAVE);

	if (bMultiPlayer && !widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	/* add central stats form */
	auto statsForm = std::make_shared<IntFormAnimated>(false);
	parent->attach(statsForm);
	statsForm->id = IDDES_STATSFORM;
	statsForm->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->setGeometry(DES_STATSFORMX, DES_STATSFORMY, DES_STATSFORMWIDTH, DES_STATSFORMHEIGHT);
			}));

	/* Add the body form */
	sFormInit.formID = IDDES_STATSFORM;
	sFormInit.id = IDDES_BODYFORM;
	sFormInit.style = WFORM_CLICKABLE | WFORM_NOCLICKMOVE;
	sFormInit.width = DES_BARFORMWIDTH;
	sFormInit.height = DES_BARFORMHEIGHT;
	sFormInit.x = DES_BARFORMX;
	sFormInit.y = DES_BARFORMY;
	sFormInit.pDisplay = intDisplayStatForm;
	auto bodyForm = widgAddForm(psWScreen, &sFormInit);
	if (!bodyForm)
	{
		return false;
	}

	/* Add the graphs for the Body */
	sBarInit.formID = IDDES_BODYFORM;
	sBarInit.id = IDDES_BODYARMOUR_K;
	sBarInit.x = DES_CLICKBARX;
//DES_CLICKBARY;
	sBarInit.y = DES_STATBAR_Y1;
	sBarInit.width = DES_CLICKBARWIDTH;
	sBarInit.height = DES_CLICKBARHEIGHT;
	sBarInit.size = 50;
	sBarInit.sCol.byte.r = DES_CLICKBARMAJORRED;
	sBarInit.sCol.byte.g = DES_CLICKBARMAJORGREEN;
	sBarInit.sCol.byte.b = DES_CLICKBARMAJORBLUE;
	sBarInit.sMinorCol.byte.r = DES_CLICKBARMINORRED;
	sBarInit.sMinorCol.byte.g = DES_CLICKBARMINORGREEN;
	sBarInit.sMinorCol.byte.b = DES_CLICKBARMINORBLUE;
	sBarInit.pTip = _("Kinetic Armour");
	sBarInit.iRange = getDesignMaxBodyArmour(WC_KINETIC);
	bodyForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

	sBarInit.id = IDDES_BODYARMOUR_H;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
	sBarInit.y = DES_STATBAR_Y2;
	sBarInit.pTip = _("Thermal Armour");
	sBarInit.iRange = getDesignMaxBodyArmour(WC_HEAT);
	bodyForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

	sBarInit.id = IDDES_BODYPOWER;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
	sBarInit.y = DES_STATBAR_Y3;
	sBarInit.pTip = _("Engine Output");
	sBarInit.iRange = getDesignMaxEngineOutput();
	bodyForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

	sBarInit.id = IDDES_BODYWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
	sBarInit.y = DES_STATBAR_Y4;
	sBarInit.pTip = _("Weight");
	sBarInit.iRange = findMaxWeight(bodyIterator());
	bodyForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

	/* Add the labels for the Body */
	sLabInit.formID = IDDES_BODYFORM;
	sLabInit.id = IDDES_BODYARMOURKLAB;
	sLabInit.x = DES_CLICKBARNAMEX;
	sLabInit.y = DES_CLICKBARY - DES_CLICKBARHEIGHT / 3;
	sLabInit.width = DES_CLICKBARNAMEWIDTH;
	sLabInit.height = DES_CLICKBARHEIGHT;
	sLabInit.pTip = _("Kinetic Armour");
	sLabInit.pDisplay = intDisplayImage;
	//just to confuse things even more - the graphics were named incorrectly!
	sLabInit.UserData = IMAGE_DES_ARMOUR_EXPLOSIVE;
	if (!widgAddLabel(psWScreen, &sLabInit))
	{
		return true;
	}
	sLabInit.id = IDDES_BODYARMOURHLAB;
	sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
	sLabInit.pTip = _("Thermal Armour");
	sLabInit.pDisplay = intDisplayImage;
	sLabInit.UserData = IMAGE_DES_ARMOUR_KINETIC;
	if (!widgAddLabel(psWScreen, &sLabInit))
	{
		return true;
	}
	sLabInit.id = IDDES_BODYPOWERLAB;
	sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
	sLabInit.pTip = _("Engine Output");
	sLabInit.pDisplay = intDisplayImage;
	sLabInit.UserData = IMAGE_DES_POWER;
	if (!widgAddLabel(psWScreen, &sLabInit))
	{
		return true;
	}
	sLabInit.id = IDDES_BODYWEIGHTLAB;
	sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
	sLabInit.pTip = _("Weight");
	sLabInit.pDisplay = intDisplayImage;
	sLabInit.UserData = IMAGE_DES_WEIGHT;
	if (!widgAddLabel(psWScreen, &sLabInit))
	{
		return true;
	}

	/* add power/points bar subform */
	sFormInit = W_FORMINIT();
	sFormInit.formID = IDDES_FORM;
	sFormInit.id = IDDES_POWERFORM;
	sFormInit.style = WFORM_PLAIN;
	sFormInit.x = DES_POWERFORMX;
	sFormInit.y = DES_POWERFORMY;
	sFormInit.width = DES_POWERFORMWIDTH;
	sFormInit.height = DES_POWERFORMHEIGHT;
	sFormInit.pDisplay = intDisplayDesignForm;
	auto powerForm = widgAddForm(psWScreen, &sFormInit);
	if (!powerForm)
	{
		return false;
	}

	/* Add the design template power bar and label*/
	sLabInit.formID = IDDES_POWERFORM;
	sLabInit.id = IDDES_TEMPPOWERLAB;
	sLabInit.x = DES_POWERX;
	sLabInit.y = DES_POWERY;
	sLabInit.pTip = _("Total Power Required");
	sLabInit.pDisplay = intDisplayImage;
	sLabInit.UserData = IMAGE_DES_POWER;
	widgAddLabel(psWScreen, &sLabInit);

	sBarInit = W_BARINIT();
	sBarInit.formID = IDDES_POWERFORM;
	sBarInit.id = IDDES_POWERBAR;
	sBarInit.x = (SWORD)(DES_POWERX + DES_POWERSEPARATIONX +
		iV_GetImageWidth(IntImages, IMAGE_DES_BODYPOINTS));
	sBarInit.y = DES_POWERY;
	sBarInit.width = (SWORD)(DES_POWERFORMWIDTH - 15 -
		iV_GetImageWidth(IntImages, IMAGE_DES_BODYPOINTS));
	sBarInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_POWERBACK);
	sBarInit.pTip = _("Total Power Required");
//WBAR_SCALE;
	sBarInit.iRange = DBAR_TEMPLATEMAXPOWER;
	powerForm->attach(DesignPowerBar::makeLessIsBetter(&sBarInit));

	/* Add the design template body points bar and label*/
	sLabInit.formID = IDDES_POWERFORM;
	sLabInit.id = IDDES_TEMPBODYLAB;
	sLabInit.x = DES_POWERX;
	sLabInit.y = (SWORD)(DES_POWERY + DES_POWERSEPARATIONY +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODYPOINTS));
	sLabInit.pTip = _("Total Body Points");
	sLabInit.pDisplay = intDisplayImage;
	sLabInit.UserData = IMAGE_DES_BODYPOINTS;
	widgAddLabel(psWScreen, &sLabInit);

	sBarInit = W_BARINIT();
	sBarInit.formID = IDDES_POWERFORM;
	sBarInit.id = IDDES_BODYPOINTS;
	sBarInit.x = (SWORD)(DES_POWERX + DES_POWERSEPARATIONX +
		iV_GetImageWidth(IntImages, IMAGE_DES_BODYPOINTS));
	sBarInit.y = (SWORD)(DES_POWERY + DES_POWERSEPARATIONY + 4 +
		iV_GetImageHeight(IntImages, IMAGE_DES_BODYPOINTS));
	sBarInit.width = (SWORD)(DES_POWERFORMWIDTH - 15 -
		iV_GetImageWidth(IntImages, IMAGE_DES_BODYPOINTS));
	sBarInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_POWERBACK);
	sBarInit.pTip = _("Total Body Points");
	sBarInit.iRange = DBAR_TEMPLATEMAXPOINTS;
	powerForm->attach(std::make_shared<DesignPowerBar>(&sBarInit));

	/* Add the variable bits of the design screen and set the bar graphs */
	desCompMode = IDES_NOCOMPONENT;
	desSysMode = IDES_NOSYSTEM;
	desPropMode = IDES_NOPROPULSION;
	intSetDesignStats(&sCurrDesign);
	intSetBodyPoints(&sCurrDesign);
	intSetDesignPower(&sCurrDesign);
	intSetDesignMode(IDES_BODY);

	/* hide design and component forms until required */
	desForm->show(bShowCentreScreen);
	statsForm->hide();
	widgHide(psWScreen, IDDES_RIGHTBASE);

	return true;
}

/* set up droid templates before going into design screen */
void desSetupDesignTemplates()
{
	/* init template list */
	apsTemplateList.clear();
	apsTemplateList.push_back(&sDefaultDesignTemplate);
	for (DroidTemplate& templ : localTemplates)
	{
		/* add template to list if not a transporter,
		 * cyborg, person or command droid,
		 */
		if (templ.type != DROID_TRANSPORTER &&
        templ.type != DROID_SUPERTRANSPORTER &&
        templ.type != DROID_CYBORG &&
        templ.type != DROID_CYBORG_SUPER &&
        templ.type != DROID_CYBORG_CONSTRUCT &&
        templ.type != DROID_CYBORG_REPAIR &&
        templ.type != DROID_PERSON &&
        researchedTemplate(&templ, selectedPlayer, includeRedundantDesigns))
		{
			apsTemplateList.push_back(&templ);
		}
	}
}

/* Add the design template form */
static bool intAddTemplateForm(DroidTemplate* psSelected)
{
	auto const& parent = psWScreen->psForm;

	/* add a form to place the tabbed form on */
	auto templbaseForm = std::make_shared<IntFormAnimated>(false);
	parent->attach(templbaseForm);
	templbaseForm->id = IDDES_TEMPLBASE;
	templbaseForm->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->setGeometry(RET_X, DESIGN_Y, RET_FORMWIDTH, DES_LEFTFORMHEIGHT);
			}));

	// Add the obsolete items button.
	makeObsoleteButton(templbaseForm);

	/* Add the design templates form */
	auto templList = IntListTabWidget::make();
	templbaseForm->attach(templList);
	templList->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			auto templList = dynamic_cast<IntListTabWidget *>(psWidget);
			assert(templList != nullptr);
			templList->setChildSize(DES_TABBUTWIDTH, DES_TABBUTHEIGHT);
			templList->setChildSpacing(DES_TABBUTGAP, DES_TABBUTGAP);
			if (auto templbaseForm = templList->parent())
			{
			int templListWidth = OBJ_BUTWIDTH * 2 + DES_TABBUTGAP;
			templList->setGeometry((RET_FORMWIDTH - templListWidth) / 2, 18, templListWidth, templbaseForm->height() -
				18);
			}
			}));

	/* Put the buttons on it */
	return intAddTemplateButtons(templList.get(), psSelected);
}

/* Add the droid template buttons to a form */
static bool intAddTemplateButtons(ListTabWidget* templList, DroidTemplate* psSelected)
{
	char TempString[256];

	/* Set up the button struct */
	int nextButtonId = IDDES_TEMPLSTART;

	/* Add each button */
	W_BARINIT sBarInit;
	sBarInit.id = IDDES_BARSTART;
	sBarInit.x = STAT_TIMEBARX;
	sBarInit.y = STAT_TIMEBARY;
	sBarInit.width = STAT_PROGBARWIDTH;
	sBarInit.height = STAT_PROGBARHEIGHT;
	sBarInit.size = 50;
	sBarInit.sCol = WZCOL_ACTION_PROGRESS_BAR_MAJOR;
	sBarInit.sMinorCol = WZCOL_ACTION_PROGRESS_BAR_MINOR;
	sBarInit.pTip = _("Power Usage");

	droidTemplID = 0;
	for (DroidTemplate* psTempl : apsTemplateList)
	{
		/* Set the tip and add the button */
		auto button = std::make_shared<IntStatsButton>();
		templList->attach(button);
		button->id = nextButtonId;
		button->setStatsAndTip(psTempl);
		templList->addWidgetToLayout(button);

		sBarInit.iRange = POWERPOINTS_DROIDDIV;
		sBarInit.size = calcTemplatePower(psTempl) / POWERPOINTS_DROIDDIV;
		if (sBarInit.size > WBAR_SCALE)
		{
			sBarInit.size = WBAR_SCALE;
		}

		ssprintf(TempString, "%s - %d", _("Power Usage"), calcTemplatePower(psTempl));
		sBarInit.pTip = TempString;
		sBarInit.formID = nextButtonId;
		if (!widgAddBarGraph(psWScreen, &sBarInit))
		{
			return false;
		}

		/* if the current template matches psSelected lock the button */
		if (psTempl == psSelected)
		{
			droidTemplID = nextButtonId;
			button->setState(WBUT_LOCK);
			templList->setCurrentPage(templList->pages() - 1);
		}

		/* Update the init struct for the next button */
		sBarInit.id += 1;
		++nextButtonId;
		//check don't go over max templates that can fit on the form
		if (nextButtonId >= IDDES_TEMPLEND)
		{
			break;
		}
	}

	return true;
}


/* Set the current mode of the design screen, and display the appropriate
 * component lists
 * added case IDES_TURRET_A,IDES_TURRET_B
 */
static void intSetDesignMode(DES_COMPMODE newCompMode, bool forceRefresh)
{
	unsigned weaponIndex;

	if (newCompMode == desCompMode && !forceRefresh)
	{
		return;
	}
	/* Have to change the component display - remove the old one */
	if (desCompMode != IDES_NOCOMPONENT)
	{
		widgDelete(psWScreen, IDDES_RIGHTBASE);

		widgSetButtonState(psWScreen, IDDES_BODYFORM, 0);
		widgSetButtonState(psWScreen, IDDES_PROPFORM, 0);
		widgSetButtonState(psWScreen, IDDES_SYSTEMFORM, 0);
		widgHide(psWScreen, IDDES_BODYFORM);
		widgHide(psWScreen, IDDES_PROPFORM);
		widgHide(psWScreen, IDDES_SYSTEMFORM);

		widgSetButtonState(psWScreen, IDDES_BODYBUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_PROPBUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_SYSTEMBUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_WPABUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_WPBBUTTON, 0);
	}

	ListTabWidget* compList;

	/* Set up the display for the new mode */
	desCompMode = newCompMode;
	switch (desCompMode)
	{
	case IDES_NOCOMPONENT:
		/* Nothing to display */
		break;
	case IDES_SYSTEM:
		compList = intAddComponentForm();
		intAddExtraSystemButtons(compList,
		                         sCurrDesign.asParts[COMPONENT_TYPE::SENSOR],
		                         sCurrDesign.asParts[COMPONENT_TYPE::ECM],
		                         sCurrDesign.asParts[COMPONENT_TYPE::CONSTRUCT],
		                         sCurrDesign.asParts[COMPONENT_TYPE::REPAIRUNIT],
		                         sCurrDesign.asParts[COMPONENT_TYPE::BRAIN]);
		intAddSystemButtons(IDES_SYSTEM);
		widgSetButtonState(psWScreen, IDDES_SYSTEMFORM, WBUT_LOCK);
		widgSetButtonState(psWScreen, IDDES_SYSTEMBUTTON, WBUT_CLICKLOCK);
		widgReveal(psWScreen, IDDES_SYSTEMFORM);
		break;
	case IDES_TURRET:
		compList = intAddComponentForm();
		weaponIndex = (sCurrDesign.weaponCount > 0) ? sCurrDesign.asWeaps[0] : 0;
		intAddComponentButtons(compList, weaponIterator(), weaponIndex, true);
		intAddSystemButtons(IDES_TURRET);
		widgSetButtonState(psWScreen, IDDES_SYSTEMFORM, WBUT_LOCK);
		widgSetButtonState(psWScreen, IDDES_SYSTEMBUTTON, WBUT_CLICKLOCK);
		widgReveal(psWScreen, IDDES_SYSTEMFORM);
		intSetSystemForm((ComponentStats*)(asWeaponStats + sCurrDesign.asWeaps[0]));
	// in case previous was a different slot
		break;
	case IDES_BODY:
		compList = intAddComponentForm();
		intAddComponentButtons(compList, bodyIterator(), sCurrDesign.asParts[COMPONENT_TYPE::BODY], false);
		widgSetButtonState(psWScreen, IDDES_BODYFORM, WBUT_LOCK);
		widgSetButtonState(psWScreen, IDDES_BODYBUTTON, WBUT_CLICKLOCK);
		widgReveal(psWScreen, IDDES_BODYFORM);
		break;
	case IDES_PROPULSION:
		compList = intAddComponentForm();
		intAddComponentButtons(compList, propulsionIterator(), sCurrDesign.asParts[COMPONENT_TYPE::PROPULSION], false);
		widgSetButtonState(psWScreen, IDDES_PROPFORM, WBUT_LOCK);
		widgSetButtonState(psWScreen, IDDES_PROPBUTTON, WBUT_CLICKLOCK);
		widgReveal(psWScreen, IDDES_PROPFORM);
		intSetPropulsionForm(asPropulsionStats + sCurrDesign.asParts[COMPONENT_TYPE::PROPULSION]);
		break;
	case IDES_TURRET_A:
		compList = intAddComponentForm();
		weaponIndex = (sCurrDesign.weaponCount > 1) ? sCurrDesign.asWeaps[1] : 0;
		intAddComponentButtons(compList, weaponIterator(), weaponIndex, true);
		intAddSystemButtons(IDES_TURRET_A);
		widgSetButtonState(psWScreen, IDDES_SYSTEMFORM, WBUT_LOCK);
		widgSetButtonState(psWScreen, IDDES_WPABUTTON, WBUT_CLICKLOCK);
		widgReveal(psWScreen, IDDES_SYSTEMFORM);
		intSetSystemForm((ComponentStats*)(asWeaponStats + sCurrDesign.asWeaps[1]));
	// in case previous was a different slot
	// Stop the button flashing
		intSetButtonFlash(IDDES_WPABUTTON, false);
		break;
	case IDES_TURRET_B:
		compList = intAddComponentForm();
		weaponIndex = (sCurrDesign.weaponCount > 2) ? sCurrDesign.asWeaps[2] : 0;
		intAddComponentButtons(compList, weaponIterator(), weaponIndex, true);
		intAddSystemButtons(IDES_TURRET_B);
		widgSetButtonState(psWScreen, IDDES_SYSTEMFORM, WBUT_LOCK);
		widgSetButtonState(psWScreen, IDDES_WPBBUTTON, WBUT_CLICKLOCK);
		widgReveal(psWScreen, IDDES_SYSTEMFORM);
		intSetSystemForm((ComponentStats*)(asWeaponStats + sCurrDesign.asWeaps[2]));
	// in case previous was a different slot
	// Stop the button flashing
		intSetButtonFlash(IDDES_WPBBUTTON, false);
		break;
	}
}

static ComponentStats*
intChooseSystemStats(DroidTemplate* psTemplate)
{
	ComponentStats* psStats = nullptr;
	int compIndex;

	// Choose correct system stats
	switch (droidTemplateType(psTemplate))
	{
	case DROID_COMMAND:
		compIndex = psTemplate->asParts[COMPONENT_TYPE::BRAIN];
		ASSERT_OR_RETURN(nullptr, compIndex < numBrainStats, "Invalid range referenced for numBrainStats, %d > %d",
		                 compIndex, numBrainStats);
		psStats = (ComponentStats*)(asBrainStats + compIndex);
		break;
	case DROID_SENSOR:
		compIndex = psTemplate->asParts[COMPONENT_TYPE::SENSOR];
		ASSERT_OR_RETURN(nullptr, compIndex < numSensorStats, "Invalid range referenced for numSensorStats, %d > %d",
		                 compIndex, numSensorStats);
		psStats = (ComponentStats*)(asSensorStats + compIndex);
		break;
	case DROID_ECM:
		compIndex = psTemplate->asParts[COMPONENT_TYPE::ECM];
		ASSERT_OR_RETURN(nullptr, compIndex < numECMStats, "Invalid range referenced for numECMStats, %d > %d",
		                 compIndex, numECMStats);
		psStats = (ComponentStats*)(asECMStats + compIndex);
		break;
	case DROID_CONSTRUCT:
	case DROID_CYBORG_CONSTRUCT:
		compIndex = psTemplate->asParts[COMPONENT_TYPE::CONSTRUCT];
		ASSERT_OR_RETURN(nullptr, compIndex < numConstructStats,
		                 "Invalid range referenced for numConstructStats, %d > %d", compIndex, numConstructStats);
		psStats = (ComponentStats*)(asConstructStats + compIndex);
		break;
	case DROID_REPAIR:
	case DROID_CYBORG_REPAIR:
		compIndex = psTemplate->asParts[COMPONENT_TYPE::REPAIRUNIT];
		ASSERT_OR_RETURN(nullptr, compIndex < numRepairStats, "Invalid range referenced for numRepairStats, %d > %d",
		                 compIndex, numRepairStats);
		psStats = (ComponentStats*)(asRepairStats + compIndex);
		break;
	case DROID_WEAPON:
	case DROID_PERSON:
	case DROID_CYBORG:
	case DROID_CYBORG_SUPER:
	case DROID_DEFAULT:
		compIndex = psTemplate->asWeaps[0];
		ASSERT_OR_RETURN(nullptr, compIndex < numWeaponStats, "Invalid range referenced for numWeaponStats, %d > %d",
		                 compIndex, numWeaponStats);
		psStats = (ComponentStats*)(asWeaponStats + compIndex);
		break;
	default:
		debug(LOG_ERROR, "unrecognised droid type");
		return nullptr;
	}

	return psStats;
}

/**
 * Checks if concatenating two `char[]` will exceed `MAX_STR_LENGTH`. If the concatenated length exceeds `MAX_STR_LENGTH`, this function will log an error.
 * @param string0 The `char[]` that `string1` will concatenate.
 * @param string1 The `char[]` to concatenate.
 */
void checkStringLength(const char* string0, const char* string1)
{
	if (strlen(string0) + strlen(string1) > MAX_STR_LENGTH)
	{
		debug(LOG_ERROR, "Name string too long %s+%s > %u", string0, string1, MAX_STR_LENGTH);
		debug(LOG_ERROR, "Please report what language you are using in the bug report!");
	}
}

const char* GetDefaultTemplateName(DroidTemplate* psTemplate)
{
	// NOTE:	At this time, savegames can support a max of 60. We are using MAX_STR_LENGTH (currently 256) for display
	// We are also returning a truncated string, instead of NULL if the string is too long.
	ComponentStats* psStats = nullptr;
	int compIndex;

	/*
		First we check for the special cases of the Transporter & Cyborgs
	*/
	if (psTemplate->type == DROID_TRANSPORTER)
	{
		sstrcpy(aCurrName, _("Transport"));
		return aCurrName;
	}
	if (psTemplate->type == DROID_SUPERTRANSPORTER)
	{
		sstrcpy(aCurrName, _("Super Transport"));
		return aCurrName;
	}

	// For cyborgs, we don't need to add the body name nor the propulsion name. We can just use the template name.
	if (psTemplate->type == DROID_CYBORG ||
      psTemplate->type == DROID_CYBORG_CONSTRUCT ||
      psTemplate->type == DROID_CYBORG_REPAIR ||
      psTemplate->type == DROID_CYBORG_SUPER)
	{
		const char* cyborgName = _(psTemplate->name.toUtf8().c_str());
		sstrcpy(aCurrName, cyborgName);
		return aCurrName;
	}

	/*
		Now get the normal default droid name based on its components
	*/
// Reset string to null
	aCurrName[0] = '\0';
	psStats = intChooseSystemStats(psTemplate);
	if (psTemplate->asWeaps[0] != 0 ||
		psTemplate->asParts[COMPONENT_TYPE::CONSTRUCT] != 0 ||
		psTemplate->asParts[COMPONENT_TYPE::SENSOR] != 0 ||
		psTemplate->asParts[COMPONENT_TYPE::ECM] != 0 ||
		psTemplate->asParts[COMPONENT_TYPE::REPAIRUNIT] != 0 ||
		psTemplate->asParts[COMPONENT_TYPE::BRAIN] != 0)
	{
		sstrcpy(aCurrName, getStatsName(psStats));
		sstrcat(aCurrName, " ");
	}

	if (psTemplate->weaponCount > 1)
	{
		sstrcat(aCurrName, _("Hydra "));
	}

	compIndex = psTemplate->asParts[COMPONENT_TYPE::BODY];
	ASSERT_OR_RETURN("", compIndex < numBodyStats, "Invalid range referenced for numBodyStats, %d > %d", compIndex,
	                 numBodyStats);
	psStats = (ComponentStats*)(asBodyStats + compIndex);
	if (psTemplate->asParts[COMPONENT_TYPE::BODY] != 0)
	{
		checkStringLength(aCurrName, getStatsName(psStats));
		sstrcat(aCurrName, getStatsName(psStats));
		sstrcat(aCurrName, " ");
	}

	compIndex = psTemplate->asParts[COMPONENT_TYPE::PROPULSION];
	ASSERT_OR_RETURN("", compIndex < numPropulsionStats, "Invalid range referenced for numPropulsionStats, %d > %d",
	                 compIndex, numPropulsionStats);
	psStats = (ComponentStats*)(asPropulsionStats + compIndex);
	if (psTemplate->asParts[COMPONENT_TYPE::PROPULSION] != 0)
	{
		checkStringLength(aCurrName, getStatsName(psStats));
		sstrcat(aCurrName, getStatsName(psStats));
	}

	return aCurrName;
}

static void intSetEditBoxTextFromTemplate(DroidTemplate* psTemplate)
{
	sstrcpy(aCurrName, "");

	/* show component names if default template else show stat name */
	if (psTemplate->type != DROID_DEFAULT)
	{
		sstrcpy(aCurrName, getStatsName(psTemplate));
	}
	else
	{
// sets aCurrName
		GetDefaultTemplateName(psTemplate);
	}

	widgSetString(psWScreen, IDDES_NAMEBOX, aCurrName);
}

/* Set all the design bar graphs from a design template */
static void intSetDesignStats(DroidTemplate* psTemplate)
{
	ComponentStats* psStats = intChooseSystemStats(psTemplate);

	/* Set system stats */
	intSetSystemForm(psStats);

	/* Set the body stats */
	intSetBodyStats(asBodyStats + psTemplate->asParts[COMPONENT_TYPE::BODY]);

	/* Set the propulsion stats */
	intSetPropulsionForm(asPropulsionStats + psTemplate->asParts[COMPONENT_TYPE::PROPULSION]);

	/* Set the name in the edit box */
	intSetEditBoxTextFromTemplate(psTemplate);
}

/* Set up the system clickable form of the design screen given a set of stats */
static bool intSetSystemForm(ComponentStats* psStats)
{
	auto newSysMode = (DES_SYSMODE)0;

	/* Figure out what the new mode should be */
	switch (psStats->compType)
	{
	case COMPONENT_TYPE::WEAPON:
		newSysMode = IDES_WEAPON;
		break;
	case COMPONENT_TYPE::SENSOR:
		newSysMode = IDES_SENSOR;
		break;
	case COMPONENT_TYPE::ECM:
		newSysMode = IDES_ECM;
		break;
	case COMPONENT_TYPE::CONSTRUCT:
		newSysMode = IDES_CONSTRUCT;
		break;
	case COMPONENT_TYPE::BRAIN:
		newSysMode = IDES_COMMAND;
		break;
	case COMPONENT_TYPE::REPAIRUNIT:
		newSysMode = IDES_REPAIR;
		break;
	default:
		ASSERT(false, "Bad choice");
	}

	/* If the correct form is already displayed just set the stats */
	if (newSysMode == desSysMode)
	{
		intSetSystemStats(psStats);

		return true;
	}

	/* Remove the old form if necessary */
	if (desSysMode != IDES_NOSYSTEM)
	{
		widgDelete(psWScreen, IDDES_SYSTEMFORM);
	}

	/* Set the new mode */
	desSysMode = newSysMode;

	/* Add the system form */
	W_FORMINIT sFormInit;
	sFormInit.formID = IDDES_STATSFORM;
	sFormInit.id = IDDES_SYSTEMFORM;
	sFormInit.style = (WFORM_CLICKABLE | WFORM_NOCLICKMOVE);
	sFormInit.x = DES_BARFORMX;
	sFormInit.y = DES_BARFORMY;
//COMPBUTWIDTH;
	sFormInit.width = DES_BARFORMWIDTH;
//COMPBUTHEIGHT;
	sFormInit.height = DES_BARFORMHEIGHT;
// set form tip to stats string
	sFormInit.pTip = getStatsName(psStats);
	sFormInit.pUserData = psStats; /* store component stats */
	sFormInit.pDisplay = intDisplayStatForm;
	auto systemForm = widgAddForm(psWScreen, &sFormInit);
	if (!systemForm)
	{
		return false;
	}

	/* Initialise the bargraph struct */
	W_BARINIT sBarInit;
	sBarInit.formID = IDDES_SYSTEMFORM;
	//sBarInit.style = WBAR_DOUBLE;
	sBarInit.x = DES_CLICKBARX;
//DES_CLICKBARY;
	sBarInit.y = DES_STATBAR_Y1;
	sBarInit.width = DES_CLICKBARWIDTH;
	sBarInit.height = DES_CLICKBARHEIGHT;
	sBarInit.sCol.byte.r = DES_CLICKBARMAJORRED;
	sBarInit.sCol.byte.g = DES_CLICKBARMAJORGREEN;
	sBarInit.sCol.byte.b = DES_CLICKBARMAJORBLUE;
	sBarInit.sMinorCol.byte.r = DES_CLICKBARMINORRED;
	sBarInit.sMinorCol.byte.g = DES_CLICKBARMINORGREEN;
	sBarInit.sMinorCol.byte.b = DES_CLICKBARMINORBLUE;

	/* Initialise the label struct */
	W_LABINIT sLabInit;
	sLabInit.formID = IDDES_SYSTEMFORM;
	sLabInit.x = DES_CLICKBARNAMEX;
	sLabInit.y = DES_CLICKBARY - DES_CLICKBARHEIGHT / 3;
	sLabInit.width = DES_CLICKBARNAMEWIDTH;
	sLabInit.height = DES_CLICKBARHEIGHT;
	sLabInit.pDisplay = intDisplayImage;

	/* See what type of system stats we've got */
	if (psStats->hasType(STAT_SENSOR))
	{
		/* Add the bar graphs*/
		sBarInit.id = IDDES_SENSORRANGE;
		sBarInit.iRange = getDesignMaxSensorRange();
		sBarInit.pTip = _("Sensor Range");
		sBarInit.denominator = TILE_UNITS;
		sBarInit.precision = 1;
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.denominator = 0;
		sBarInit.precision = 0;
		sBarInit.id = IDDES_SYSTEMSWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.iRange = findMaxWeight(extraSystemIterator());
		sBarInit.pTip = _("Weight");
		systemForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

		/* Add the labels */
		sLabInit.id = IDDES_SENSORRANGELAB;
		sLabInit.pTip = _("Sensor Range");
		sLabInit.UserData = IMAGE_DES_RANGE;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_SYSTEMSWEIGHTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
	}
	else if (psStats->hasType(STAT_ECM))
	{
		/* Add the bar graphs */
		sBarInit.id = IDDES_ECMPOWER;
		sBarInit.iRange = getDesignMaxEcmRange();
		sBarInit.pTip = _("ECM Power");
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_SYSTEMSWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.iRange = findMaxWeight(extraSystemIterator());
		sBarInit.pTip = _("Weight");
		systemForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

		/* Add the labels */
		sLabInit.id = IDDES_ECMPOWERLAB;
		sLabInit.pTip = _("ECM Power");
		sLabInit.UserData = IMAGE_DES_POWER;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_SYSTEMSWEIGHTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
	}
	else if (psStats->hasType(STAT_CONSTRUCT))
	{
		/* Add the bar graphs */
		sBarInit.id = IDDES_CONSTPOINTS;
		sBarInit.pTip = _("Build Points");
		sBarInit.iRange = getDesignMaxBuildPoints();
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_SYSTEMSWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.pTip = _("Weight");
		sBarInit.iRange = findMaxWeight(extraSystemIterator());
		systemForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

		/* Add the labels */
		sLabInit.id = IDDES_CONSTPOINTSLAB;
		sLabInit.pTip = _("Build Points");
		sLabInit.UserData = IMAGE_DES_BUILDRATE;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_SYSTEMSWEIGHTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
	}
	else if (psStats->hasType(STAT_REPAIR))
	{
		/* Add the bar graphs */
		sBarInit.id = IDDES_REPAIRPOINTS;
		sBarInit.pTip = _("Build Points");
		sBarInit.iRange = findMax(
			repairIterator(),
			[=](ComponentStats& stats) { return repairPoints((RepairStats*)&stats, selectedPlayer); }
		);
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_SYSTEMSWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.pTip = _("Weight");
		sBarInit.iRange = findMaxWeight(extraSystemIterator());
		systemForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

		/* Add the labels */
		sLabInit.id = IDDES_REPAIRPTLAB;
		sLabInit.pTip = _("Build Points");
		sLabInit.UserData = IMAGE_DES_BUILDRATE;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_REPAIRWGTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
	}
	else if (psStats->hasType(STAT_WEAPON))
	{
		/* Add the bar graphs */
		sBarInit.id = IDDES_WEAPRANGE;
		sBarInit.iRange = findMaxWeaponAttribute(proj_GetLongRange);
		sBarInit.pTip = _("Range");
		sBarInit.denominator = TILE_UNITS;
		sBarInit.precision = 1;
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.denominator = 1;
		sBarInit.precision = 0;
		sBarInit.id = IDDES_WEAPDAMAGE;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.iRange = findMaxWeaponAttribute(weaponDamage);
		sBarInit.pTip = _("Damage");
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_WEAPROF;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y3;
		sBarInit.iRange = findMaxWeaponAttribute(weaponROF);
		sBarInit.pTip = _("Rate-of-Fire");
		systemForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_SYSTEMSWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y4;
		sBarInit.iRange = findMaxWeight(weaponIterator());
		sBarInit.pTip = _("Weight");
		systemForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

		/* Add the labels */
		sLabInit.id = IDDES_WEAPRANGELAB;
		sLabInit.pTip = _("Range");
		sLabInit.UserData = IMAGE_DES_RANGE;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_WEAPDAMAGELAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Damage");
		sLabInit.UserData = IMAGE_DES_DAMAGE;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_WEAPROFLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Rate-of-Fire");
		sLabInit.UserData = IMAGE_DES_FIRERATE;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_SYSTEMSWEIGHTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
	}

	// Add the correct component form
	switch (desSysMode)
	{
	case IDES_SENSOR:
	case IDES_CONSTRUCT:
	case IDES_ECM:
	case IDES_REPAIR:
	case IDES_COMMAND:
		intSetDesignMode(IDES_SYSTEM);
		break;
	case IDES_WEAPON:
		intSetDesignMode(IDES_TURRET);
		break;
	default:
		break;
	}

	/* Set the stats */
	intSetSystemStats(psStats);

	/* Lock the form down if necessary */
	if (desCompMode == IDES_SYSTEM)
	{
		widgSetButtonState(psWScreen, IDDES_SYSTEMFORM, WBUT_LOCK);
	}

	return true;
}


/* Set up the propulsion clickable form of the design screen given a set of stats */
static bool intSetPropulsionForm(PropulsionStats* psStats)
{
	auto newPropMode = (DES_PROPMODE)0;

	ASSERT_OR_RETURN(false, psStats != nullptr, "Invalid propulsion stats pointer");

	/* figure out what the new mode should be */
	switch (asPropulsionTypes[psStats->propulsionType].travel)
	{
	case GROUND:
		newPropMode = IDES_GROUND;
		break;
	case AIR:
		newPropMode = IDES_AIR;
		break;
	}

	/* Remove the old form if necessary */
	if (desPropMode != IDES_NOPROPULSION)
	{
		widgDelete(psWScreen, IDDES_PROPFORM);
	}

	/* Set the new mode */
	desPropMode = newPropMode;

	/* Add the propulsion form */
	W_FORMINIT sFormInit;
	sFormInit.formID = IDDES_STATSFORM;
	sFormInit.id = IDDES_PROPFORM;
	sFormInit.style = WFORM_CLICKABLE | WFORM_NOCLICKMOVE;
	sFormInit.x = DES_BARFORMX;
	sFormInit.y = DES_BARFORMY;
//DES_COMPBUTWIDTH;
	sFormInit.width = DES_BARFORMWIDTH;
//DES_COMPBUTHEIGHT;
	sFormInit.height = DES_BARFORMHEIGHT;
// set form tip to stats string
	sFormInit.pTip = getStatsName(psStats);
	sFormInit.pDisplay = intDisplayStatForm;
	auto propulsionForm = widgAddForm(psWScreen, &sFormInit);
	if (!propulsionForm)
	{
		return false;
	}

	/* Initialise the bargraph struct */
	W_BARINIT sBarInit;
	sBarInit.formID = IDDES_PROPFORM;
	//sBarInit.style = WBAR_DOUBLE;
	sBarInit.x = DES_CLICKBARX;
//DES_CLICKBARY;
	sBarInit.y = DES_STATBAR_Y1;
	sBarInit.width = DES_CLICKBARWIDTH;
	sBarInit.height = DES_CLICKBARHEIGHT;
	sBarInit.sCol.byte.r = DES_CLICKBARMAJORRED;
	sBarInit.sCol.byte.g = DES_CLICKBARMAJORGREEN;
	sBarInit.sCol.byte.b = DES_CLICKBARMAJORBLUE;
	sBarInit.sMinorCol.byte.r = DES_CLICKBARMINORRED;
	sBarInit.sMinorCol.byte.g = DES_CLICKBARMINORGREEN;
	sBarInit.sMinorCol.byte.b = DES_CLICKBARMINORBLUE;

	/* Initialise the label struct */
	W_LABINIT sLabInit;
	sLabInit.formID = IDDES_PROPFORM;
	sLabInit.x = DES_CLICKBARNAMEX;
	sLabInit.y = DES_CLICKBARY - DES_CLICKBARHEIGHT / 3;
	sLabInit.width = DES_CLICKBARNAMEWIDTH;
//DES_CLICKBARHEIGHT;
	sLabInit.height = DES_CLICKBARNAMEHEIGHT;
	sLabInit.pDisplay = intDisplayImage;

	/* See what type of propulsion we've got */
	switch (desPropMode)
	{
	case IDES_AIR:
		/* Add the bar graphs */
		sBarInit.id = IDDES_PROPAIR;
		sBarInit.iRange = findMaxPropulsionSpeed(TER_ROAD);
		sBarInit.pTip = _("Air Speed");
		sBarInit.denominator = TILE_UNITS;
		sBarInit.precision = 2;
		propulsionForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.denominator = 1;
		sBarInit.precision = 0;
		sBarInit.id = IDDES_PROPWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.iRange = findMax(propulsionIterator(), calculatePropulsionWeight);
		sBarInit.pTip = _("Weight");
		propulsionForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

	/* Add the labels */
		sLabInit.id = IDDES_PROPAIRLAB;
		sLabInit.pTip = _("Air Speed");
		sLabInit.UserData = IMAGE_DES_HOVER;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_PROPWEIGHTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		break;
	case IDES_GROUND:
		/* Add the bar graphs */
		sBarInit.id = IDDES_PROPROAD;
		sBarInit.pTip = _("Road Speed");
		sBarInit.iRange = findMaxPropulsionSpeed(TER_ROAD);
		sBarInit.denominator = TILE_UNITS;
		sBarInit.precision = 2;
		propulsionForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_PROPCOUNTRY;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y2;
		sBarInit.pTip = _("Off-Road Speed");
		sBarInit.iRange = findMaxPropulsionSpeed(TER_SANDYBRUSH);
		propulsionForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.id = IDDES_PROPWATER;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y3;
		sBarInit.pTip = _("Water Speed");
		sBarInit.iRange = findMaxPropulsionSpeed(TER_WATER);
		propulsionForm->attach(std::make_shared<DesignStatsBar>(&sBarInit));

		sBarInit.denominator = 1;
		sBarInit.precision = 0;
		sBarInit.id = IDDES_PROPWEIGHT;
//+= DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sBarInit.y = DES_STATBAR_Y4;
		sBarInit.pTip = _("Weight");
		sBarInit.iRange = findMax(propulsionIterator(), calculatePropulsionWeight);
		propulsionForm->attach(DesignStatsBar::makeLessIsBetter(&sBarInit));

	/* Add the labels */
		sLabInit.id = IDDES_PROPROADLAB;
		sLabInit.pTip = _("Road Speed");
		sLabInit.UserData = IMAGE_DES_ROAD;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_PROPCOUNTRYLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Off-Road Speed");
		sLabInit.UserData = IMAGE_DES_CROSSCOUNTRY;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_PROPWATERLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Water Speed");
//WATER;
		sLabInit.UserData = IMAGE_DES_HOVER;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		sLabInit.id = IDDES_PROPWEIGHTLAB;
		sLabInit.y += DES_CLICKBARHEIGHT + DES_CLICKGAP;
		sLabInit.pTip = _("Weight");
		sLabInit.UserData = IMAGE_DES_WEIGHT;
		if (!widgAddLabel(psWScreen, &sLabInit))
		{
			return false;
		}
		break;
	default:
		break;
	}

	/* Set the stats */
	intSetPropulsionStats(psStats);

	/* Lock the form down if necessary */
	if (desCompMode == IDES_PROPULSION)
	{
		widgSetButtonState(psWScreen, IDDES_PROPFORM, WBUT_LOCK);
	}

	return true;
}

/* Add the component tab form to the design screen */
static ListTabWidget* intAddComponentForm()
{
	auto const& parent = psWScreen->psForm;

	/* add a form to place the tabbed form on */
	auto rightBase = std::make_shared<IntFormAnimated>(false);
	parent->attach(rightBase);
	rightBase->id = IDDES_RIGHTBASE;
	rightBase->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->setGeometry(RADTLX - 2, DESIGN_Y, RET_FORMWIDTH, DES_RIGHTFORMHEIGHT);
			}));

	//now a single form
	auto compList = IntListTabWidget::make();
	rightBase->attach(compList);
	compList->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			auto *compList = dynamic_cast<IntListTabWidget *>(psWidget);
			assert(compList != nullptr);
			compList->setChildSize(DES_TABBUTWIDTH, DES_TABBUTHEIGHT);
			compList->setChildSpacing(DES_TABBUTGAP, DES_TABBUTGAP);
			if (auto rightBase = compList->parent())
			{
			int objListWidth = DES_TABBUTWIDTH * 2 + DES_TABBUTGAP;
			compList->setGeometry((rightBase->width() - objListWidth) / 2, 40, objListWidth, rightBase->height() - 40);
			}
			}));
	return compList.get();
}

/* Add the system buttons (weapons, command droid, etc) to the design screen */
static bool intAddSystemButtons(DES_COMPMODE mode)
{
	// add the weapon button
	W_BUTINIT sButInit;
	sButInit.formID = IDDES_RIGHTBASE;
	sButInit.id = IDDES_WEAPONS;
	sButInit.x = DES_WEAPONBUTTON_X;
	sButInit.y = DES_SYSTEMBUTTON_Y;
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_WEAPONS);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_WEAPONS);
	sButInit.pTip = _("Weapons");
	sButInit.pDisplay = intDisplayButtonHilight;
	sButInit.UserData = PACKDWORD_TRI(0, IMAGE_DES_EXTRAHI, IMAGE_DES_WEAPONS);
	if (!widgAddButton(psWScreen, &sButInit))
	{
		return false;
	}

	// if currently got a VTOL proplusion attached then don't add the system buttons
	// don't add the system button if mode is IDES_TURRET_A or IDES_TURRET_B
	if (!checkTemplateIsVtol(&sCurrDesign) && mode != IDES_TURRET_A && mode != IDES_TURRET_B)
	{
		// add the system button
		sButInit.formID = IDDES_RIGHTBASE;
		sButInit.id = IDDES_SYSTEMS;
		sButInit.x = DES_SYSTEMBUTTON_X;
		sButInit.y = DES_SYSTEMBUTTON_Y;
		sButInit.width = iV_GetImageWidth(IntImages, IMAGE_DES_SYSTEMS);
		sButInit.height = iV_GetImageHeight(IntImages, IMAGE_DES_SYSTEMS);
		sButInit.pTip = _("Systems");
		sButInit.pDisplay = intDisplayButtonHilight;
		sButInit.UserData = PACKDWORD_TRI(0, IMAGE_DES_EXTRAHI, IMAGE_DES_SYSTEMS);
		if (!widgAddButton(psWScreen, &sButInit))
		{
			return false;
		}
		if (mode == IDES_SYSTEM)
		{
			widgSetButtonState(psWScreen, IDDES_SYSTEMS, WBUT_LOCK);
		}
	}

	// lock down the correct button
	switch (mode)
	{
	case IDES_TURRET:
	case IDES_TURRET_A:
	case IDES_TURRET_B:
		widgSetButtonState(psWScreen, IDDES_WEAPONS, WBUT_LOCK);
		break;
	case IDES_SYSTEM:
		break;
	default:
		ASSERT(!"invalid/unexpected mode", "unexpected mode");
		break;
	}

	return true;
}

/* Add the component buttons to the main tab of the component form */
static bool intAddComponentButtons(ListTabWidget* compList,
                                   const ComponentIterator& componentIterator, 
                                   unsigned compID, bool bWeapon)
{
	int bodysize = SIZE_NUM;

	/* Set up the button struct */
	int nextButtonId = IDDES_COMPSTART;

	//need to set max number of buttons possible
	unsigned maxComponents = bWeapon ? MAX_SYSTEM_COMPONENTS : MAX_DESIGN_COMPONENTS;

	/*if adding weapons - need to check if the propulsion is a VTOL*/
	bool bVTOL = false;

	if (bWeapon)
	{
		//check if the current Template propulsion has been set
		if (sCurrDesign.asParts[COMPONENT_TYPE::PROPULSION])
		{
			PropulsionStats* psPropStats = asPropulsionStats + sCurrDesign.asParts[COMPONENT_TYPE::PROPULSION];
			ASSERT_OR_RETURN(false, psPropStats != nullptr, "invalid propulsion stats pointer");

			bVTOL |= asPropulsionTypes[psPropStats->propulsionType].travel == AIR;
		}
		if (sCurrDesign.asParts[COMPONENT_TYPE::BODY])
		{
			bodysize = asBodyStats[sCurrDesign.asParts[COMPONENT_TYPE::BODY]].size;
		}
	}

	/* Add each button */
	desCompID = 0;
	numComponent = 0;

	componentIterator([&](ComponentStats& currStats, size_t index)
	{
		/* If we are out of space in the list - stop */
		if (numComponent >= maxComponents)
		{
			return false;
		}

		/*skip indirect weapons if VTOL propulsion or numVTOLattackRuns for the weapon is zero*/
		if (bWeapon)
		{
			auto& weapon = (WeaponStats&)currStats;
			if ((weapon.vtolAttackRuns > 0) != bVTOL
				|| (weapon.weaponSize == WEAPON_SIZE_LIGHT && bodysize != SIZE_LIGHT)
				|| (weapon.weaponSize == WEAPON_SIZE_HEAVY && bodysize == SIZE_LIGHT))
			{
				return true;
			}
		}

		/* Set the tip and add the button */
		auto button = std::make_shared<IntStatsButton>();
		compList->attach(button);
		button->id = nextButtonId;
		button->setStatsAndTip(&currStats);
		compList->addWidgetToLayout(button);

		/* Store the stat pointer in the list */
		apsComponentList[numComponent++] = &currStats;

		/* If this matches the component ID lock the button */
		if (index == compID)
		{
			desCompID = nextButtonId;
			button->setState(WBUT_LOCK);
			compList->setCurrentPage(compList->pages() - 1);
		}

		/* Update the init struct for the next button */
		++nextButtonId;

		return true;
	});

	widgSetBarRange(psWScreen, IDDES_BODYPOINTS, findMax(componentIterator, calcShadowBodyPoints));
	widgSetBarRange(psWScreen, IDDES_POWERBAR, findMax(componentIterator, calcShadowPower));

	return true;
}

/* Add the component buttons to the main tab of the component form */
static bool intAddExtraSystemButtons(ListTabWidget* compList, unsigned sensorIndex, unsigned ecmIndex,
                                     unsigned constIndex, unsigned repairIndex, unsigned brainIndex)
{
	unsigned compIndex = 0;

	// Set up the button struct
	int nextButtonId = IDDES_EXTRASYSSTART;

	// Add the buttons :
	// buttonType == 0  -  Sensor Buttons
	// buttonType == 1  -  ECM Buttons
	// buttonType == 2  -  Constructor Buttons
	// buttonType == 3  -  Repair Buttons
	// buttonType == 4  -  Brain Buttons
	numExtraSys = 0;
	for (auto buttonType = 0; buttonType < 5; buttonType++)
	{
		ComponentIterator componentIterator;
		switch (buttonType)
		{
		case 0:
			// Sensor Buttons
			componentIterator = sensorIterator();
			compIndex = sensorIndex;
			break;
		case 1:
			// ECM Buttons
			componentIterator = ecmIterator();
			compIndex = ecmIndex;
			break;
		case 2:
			// Constructor Buttons
			componentIterator = constructorIterator();
			compIndex = constIndex;
			break;
		case 3:
			// Repair Buttons
			componentIterator = repairIterator();
			compIndex = repairIndex;
			break;
		case 4:
			// Brain Buttons
			componentIterator = brainIterator();
			compIndex = brainIndex;
			break;
		}

		componentIterator([&](ComponentStats& stats, size_t i)
		{
			// If we are out of space in the list - stop
			if (numExtraSys >= MAXEXTRASYS)
			{
				ASSERT(false, "Too many components for the list");
				return false;
			}

			// Set the tip and add the button
			auto button = std::make_shared<IntStatsButton>();
			compList->attach(button);
			button->id = nextButtonId;
			button->setStatsAndTip(&stats);
			compList->addWidgetToLayout(button);

			//just use one set of buffers for mixed system form
			if (stats.compType == COMPONENT_TYPE::BRAIN)
			{
				button->setStats(((CommanderStats*)&stats)->psWeaponStat);
			}

			// Store the stat pointer in the list
			apsExtraSysList[numExtraSys++] = &stats;

			// If this matches the sensorIndex note the form and button
			if (i == compIndex)
			{
				desCompID = nextButtonId;
				button->setState(WBUT_LOCK);
				compList->setCurrentPage(compList->pages() - 1);
			}

			// Update the init struct for the next button
			++nextButtonId;
			return true;
		});
	}

	widgSetBarRange(psWScreen, IDDES_BODYPOINTS, findMax(extraSystemIterator(), calcShadowBodyPoints));
	widgSetBarRange(psWScreen, IDDES_POWERBAR, findMax(extraSystemIterator(), calcShadowPower));

	return true;
}


/* Set the bar graphs for the system clickable */
static void intSetSystemStats(ComponentStats* psStats)
{
	W_FORM* psForm;

	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");

	/* set form tip to stats string */
	widgSetTip(psWScreen, IDDES_SYSTEMFORM, checkIfZNullStat(psStats) ? "" : getStatsName(psStats));

	/* set form stats for later display in intDisplayStatForm */
	psForm = (W_FORM*)widgGetFromID(psWScreen, IDDES_SYSTEMFORM);
	if (psForm != nullptr)
	{
		psForm->pUserData = psStats;
	}

	/* Set the correct system stats */
	switch (psStats->compType)
	{
	case COMPONENT_TYPE::SENSOR:
		intSetSensorStats((SensorStats*)psStats);
		break;
	case COMPONENT_TYPE::ECM:
		intSetECMStats((EcmStats*)psStats);
		break;
	case COMPONENT_TYPE::WEAPON:
		intSetWeaponStats((WeaponStats*)psStats);
		break;
	case COMPONENT_TYPE::CONSTRUCT:
		intSetConstructStats((ConstructStats*)psStats);
		break;
	case COMPONENT_TYPE::REPAIRUNIT:
		intSetRepairStats((RepairStats*)psStats);
		break;
	case COMPONENT_TYPE::BRAIN:
		// ??? TBD FIXME
		break;
	default:
		ASSERT(false, "Bad choice");
	}
}

/* Set the shadow bar graphs for the system clickable */
static void intSetSystemShadowStats(ComponentStats* psStats)
{
	switch (desSysMode)
	{
	case IDES_WEAPON:
		intSetWeaponShadowStats(psStats && psStats->compType == COMPONENT_TYPE::WEAPON ? (WeaponStats*)psStats : nullptr);
		return;
	case IDES_SENSOR:
		intSetSensorShadowStats(psStats && psStats->compType == COMPONENT_TYPE::SENSOR ? (SensorStats*)psStats : nullptr);
		break;
	case IDES_ECM:
		intSetECMShadowStats(psStats && psStats->compType == COMPONENT_TYPE::ECM ? (EcmStats*)psStats : nullptr);
		break;
	case IDES_CONSTRUCT:
		intSetConstructShadowStats(psStats && psStats->compType == COMPONENT_TYPE::CONSTRUCT
			                           ? (ConstructStats*)psStats
			                           : nullptr);
		break;
	case IDES_REPAIR:
		intSetRepairShadowStats(psStats && psStats->compType == COMPONENT_TYPE::REPAIRUNIT ? (RepairStats*)psStats : nullptr);
		break;
	default:
		return;
	}

	widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats ? psStats->weight : 0);
}

/* Set the bar graphs for the sensor stats */
static void intSetSensorStats(SensorStats* psStats)
{
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_SENSOR), "stats have wrong type");

	/* range */
	widgSetBarSize(psWScreen, IDDES_SENSORRANGE, sensorRange(psStats, selectedPlayer));
	/* weight */
	widgSetBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
}

/* Set the shadow bar graphs for the sensor stats */
static void intSetSensorShadowStats(SensorStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_SENSOR), "stats have wrong type");

	if (psStats)
	{
		/* range */
		widgSetMinorBarSize(psWScreen, IDDES_SENSORRANGE,
		                    sensorRange(psStats, (UBYTE)selectedPlayer));

		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
	}
	else
	{
		/* Remove the shadow bars */
		widgSetMinorBarSize(psWScreen, IDDES_SENSORRANGE, 0);
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, 0);
	}
}


/* Set the bar graphs for the ECM stats */
static void intSetECMStats(EcmStats* psStats)
{
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_ECM), "stats have wrong type");

	/* range */
	widgSetBarSize(psWScreen, IDDES_ECMPOWER, ecmRange(psStats, selectedPlayer));
	/* weight */
	widgSetBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
}

/* Set the shadow bar graphs for the ECM stats */
static void intSetECMShadowStats(EcmStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_ECM), "stats have wrong type");

	if (psStats)
	{
		/* power */
		widgSetMinorBarSize(psWScreen, IDDES_ECMPOWER, ecmRange(psStats, (UBYTE)selectedPlayer));
		/* weight */
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
	}
	else
	{
		/* Remove the shadow bars */
		widgSetMinorBarSize(psWScreen, IDDES_ECMPOWER, 0);
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, 0);
	}
}


/* Set the bar graphs for the Constructor stats */
static void intSetConstructStats(ConstructStats* psStats)
{
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_CONSTRUCT), "stats have wrong type");

	/* power */
	widgSetBarSize(psWScreen, IDDES_CONSTPOINTS,
	               constructorPoints(psStats, (UBYTE)selectedPlayer));
	/* weight */
	widgSetBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
}


/* Set the shadow bar graphs for the Constructor stats */
static void intSetConstructShadowStats(ConstructStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_CONSTRUCT), "stats have wrong type");

	if (psStats)
	{
		/* power */
		widgSetMinorBarSize(psWScreen, IDDES_CONSTPOINTS,
		                    constructorPoints(psStats, (UBYTE)selectedPlayer));
		/* weight */
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
	}
	else
	{
		/* reset the shadow bars */
		widgSetMinorBarSize(psWScreen, IDDES_CONSTPOINTS, 0);
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, 0);
	}
}

/* Set the bar graphs for the Repair stats */
static void intSetRepairStats(RepairStats* psStats)
{
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_REPAIR), "stats have wrong type");

	/* power */
	widgSetBarSize(psWScreen, IDDES_REPAIRPOINTS,
	               repairPoints(psStats, (UBYTE)selectedPlayer));
	/* weight */
	widgSetBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
}


/* Set the shadow bar graphs for the Repair stats */
static void intSetRepairShadowStats(RepairStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_REPAIR), "stats have wrong type");

	if (psStats)
	{
		/* power */
		widgSetMinorBarSize(psWScreen, IDDES_REPAIRPOINTS,
		                    repairPoints(psStats, (UBYTE)selectedPlayer));
		/* weight */
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
	}
	else
	{
		/* reset the shadow bars */
		widgSetMinorBarSize(psWScreen, IDDES_REPAIRPOINTS, 0);
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, 0);
	}
}


/* Set the bar graphs for the Weapon stats */
static void intSetWeaponStats(WeaponStats* psStats)
{
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_WEAPON), "stats have wrong type");

	/* range */
	widgSetBarSize(psWScreen, IDDES_WEAPRANGE, proj_GetLongRange(psStats, selectedPlayer));
	/* rate of fire */
	widgSetBarSize(psWScreen, IDDES_WEAPROF, weaponROF(psStats, (SBYTE)selectedPlayer));
	/* damage */
	widgSetBarSize(psWScreen, IDDES_WEAPDAMAGE, (UWORD)weaponDamage(psStats,
	                                                                (UBYTE)selectedPlayer));
	/* weight */
	widgSetBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
}

/* Set the shadow bar graphs for the Weapon stats */
static void intSetWeaponShadowStats(WeaponStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_WEAPON), "stats have wrong type");

	if (psStats)
	{
		/* range */
		widgSetMinorBarSize(psWScreen, IDDES_WEAPRANGE, proj_GetLongRange(psStats, selectedPlayer));
		/* rate of fire */
		widgSetMinorBarSize(psWScreen, IDDES_WEAPROF, weaponROF(psStats, (SBYTE)selectedPlayer));
		/* damage */
		widgSetMinorBarSize(psWScreen, IDDES_WEAPDAMAGE, (UWORD)weaponDamage(
			                    psStats, (UBYTE)selectedPlayer));
		/* weight */
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, psStats->weight);
	}
	else
	{
		/* Reset the shadow bars */
		widgSetMinorBarSize(psWScreen, IDDES_WEAPRANGE, 0);
		widgSetMinorBarSize(psWScreen, IDDES_WEAPROF, 0);
		widgSetMinorBarSize(psWScreen, IDDES_WEAPDAMAGE, 0);
		widgSetMinorBarSize(psWScreen, IDDES_SYSTEMSWEIGHT, 0);
	}
}

/* Set the bar graphs for the Body stats */
static void intSetBodyStats(BodyStats* psStats)
{
	W_FORM* psForm;

	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_BODY), "stats have wrong type");

	/* set form tip to stats string */
	widgSetTip(psWScreen, IDDES_BODYFORM, checkIfZNullStat(psStats) ? "" : getStatsName(psStats));

	/* armour */
	//do kinetic armour
	widgSetBarSize(psWScreen, IDDES_BODYARMOUR_K, bodyArmour(psStats, selectedPlayer, WC_KINETIC));
	//do heat armour
	widgSetBarSize(psWScreen, IDDES_BODYARMOUR_H, bodyArmour(psStats, selectedPlayer, WC_HEAT));
	/* power */
	widgSetBarSize(psWScreen, IDDES_BODYPOWER, bodyPower(psStats, selectedPlayer));
	/* weight */
	widgSetBarSize(psWScreen, IDDES_BODYWEIGHT, psStats->weight);

	/* set form stats for later display in intDisplayStatForm */
	psForm = (W_FORM*)widgGetFromID(psWScreen, IDDES_BODYFORM);
	if (psForm != nullptr)
	{
		psForm->pUserData = psStats;
	}
}

/* Set the shadow bar graphs for the Body stats */
static void intSetBodyShadowStats(BodyStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_BODY), "stats have wrong type");

	if (psStats)
	{
		/* armour - kinetic*/
		widgSetMinorBarSize(psWScreen, IDDES_BODYARMOUR_K, bodyArmour(psStats, selectedPlayer, WC_KINETIC));
		//armour - heat
		widgSetMinorBarSize(psWScreen, IDDES_BODYARMOUR_H, bodyArmour(psStats, selectedPlayer, WC_HEAT));
		/* power */
		widgSetMinorBarSize(psWScreen, IDDES_BODYPOWER, bodyPower(psStats, selectedPlayer));
		/* weight */
		widgSetMinorBarSize(psWScreen, IDDES_BODYWEIGHT, psStats->weight);
	}
	else
	{
		/* Reset the shadow bars */
		widgSetMinorBarSize(psWScreen, IDDES_BODYARMOUR_K, 0);
		widgSetMinorBarSize(psWScreen, IDDES_BODYARMOUR_H, 0);
		widgSetMinorBarSize(psWScreen, IDDES_BODYPOWER, 0);
		widgSetMinorBarSize(psWScreen, IDDES_BODYWEIGHT, 0);
	}
}

/* Sets the Design Power Bar for a given Template */
static void intSetDesignPower(DroidTemplate* psTemplate)
{
	/* use the same scale as PowerBar in main window so values are relative */
	widgSetBarSize(psWScreen, IDDES_POWERBAR, calcTemplatePower(psTemplate));
}

static void setTemplateStat(DroidTemplate* psTemplate, ComponentStats* psStats)
{
	ASSERT_OR_RETURN(, psStats != nullptr, "psStats not null");

	auto clearWeapons = [&](int8_t newNumWeaps)
	{
		for (int i = newNumWeaps; i < MAX_WEAPONS; ++i)
		{
			psTemplate->asWeaps[i] = 0;
		}
		psTemplate->weaponCount = std::min(psTemplate->weaponCount, newNumWeaps);
	};

	auto clearNonWeapons = [&]
	{
		// Reset the sensor, ECM and constructor and repair
		// - defaults will be set when OK is hit
		psTemplate->asParts[COMPONENT_TYPE::BRAIN] = 0;
		psTemplate->asParts[COMPONENT_TYPE::REPAIRUNIT] = 0;
		psTemplate->asParts[COMPONENT_TYPE::ECM] = 0;
		psTemplate->asParts[COMPONENT_TYPE::SENSOR] = 0;
		psTemplate->asParts[COMPONENT_TYPE::CONSTRUCT] = 0;
	};

	auto clearTurret = [&]
	{
		clearNonWeapons();
		clearWeapons(0);
	};

	switch (psStats->compType)
	{
	case COMPONENT_TYPE::BODY:
		{
			auto stats = (BodyStats*)psStats;
			psTemplate->asParts[COMPONENT_TYPE::BODY] = stats - asBodyStats;
			if (!intCheckValidWeaponForProp(psTemplate))
			{
				clearTurret();
			}
			else
			{
				clearWeapons(stats->weaponSlots);
			}
			break;
		}
	case COMPONENT_TYPE::BRAIN:
		{
			auto stats = (CommanderStats*)psStats;
			clearTurret();
			psTemplate->asParts[COMPONENT_TYPE::BRAIN] = stats - asBrainStats;
			psTemplate->asWeaps[0] = stats->psWeaponStat - asWeaponStats;
			psTemplate->weaponCount = 1;
			break;
		}
	case COMPONENT_TYPE::PROPULSION:
		{
			auto stats = (PropulsionStats*)psStats;
			auto oldStats = &asPropulsionStats[psTemplate->asParts[COMPONENT_TYPE::PROPULSION]];
			if ((stats->propulsionType == PROPULSION_TYPE::LIFT) != (oldStats->propulsionType == PROPULSION_TYPE::LIFT))
			{
				clearTurret();
			}
			psTemplate->asParts[COMPONENT_TYPE::PROPULSION] = stats - asPropulsionStats;
			break;
		}
	case COMPONENT_TYPE::REPAIRUNIT:
		clearTurret();
		psTemplate->asParts[COMPONENT_TYPE::REPAIRUNIT] = (RepairStats*)psStats - asRepairStats;
		break;
	case COMPONENT_TYPE::ECM:
		clearTurret();
		psTemplate->asParts[COMPONENT_TYPE::ECM] = (EcmStats*)psStats - asECMStats;
		break;
	case COMPONENT_TYPE::SENSOR:
		clearTurret();
		psTemplate->asParts[COMPONENT_TYPE::SENSOR] = (SensorStats*)psStats - asSensorStats;
		break;
	case COMPONENT_TYPE::CONSTRUCT:
		clearTurret();
		psTemplate->asParts[COMPONENT_TYPE::CONSTRUCT] = (ConstructStats*)psStats - asConstructStats;
		break;
	case COMPONENT_TYPE::WEAPON:
		{
			clearNonWeapons();
			int i = desCompMode == IDES_TURRET_A ? 1 : desCompMode == IDES_TURRET_B ? 2 : 0;
			psTemplate->asWeaps[i] = (WeaponStats*)psStats - asWeaponStats;
			psTemplate->weaponCount = std::max<int>(psTemplate->weaponCount, i + 1);
			break;
		}
	case COMPONENT_TYPE::COUNT:
		ASSERT(false, "COMPONENT_TYPE::COUNT isn't a component type");
		break;
	}
}

/* Set the shadow bar graphs for the template power points - psStats is new hilited stats*/
static void intSetTemplatePowerShadowStats(ComponentStats* psStats)
{
	if (!psStats)
	{
		/* Reset the shadow bar */
		widgSetMinorBarSize(psWScreen, IDDES_POWERBAR, 0);
		return;
	}

	widgSetMinorBarSize(psWScreen, IDDES_POWERBAR, calcShadowPower(*psStats));
}

/* Sets the Body Points Bar for a given Template */
static void intSetBodyPoints(DroidTemplate* psTemplate)
{
	// If total greater than Body Bar size then scale values.
	widgSetBarSize(psWScreen, IDDES_BODYPOINTS, calcTemplateBody(psTemplate, selectedPlayer));
}

/* Set the shadow bar graphs for the template Body points - psStats is new hilited stats*/
static void intSetTemplateBodyShadowStats(ComponentStats* psStats)
{
	if (!psStats)
	{
		/* Reset the shadow bar */
		widgSetMinorBarSize(psWScreen, IDDES_BODYPOINTS, 0);
		return;
	}

	widgSetMinorBarSize(psWScreen, IDDES_BODYPOINTS, calcShadowBodyPoints(*psStats));
}


/* Calculate the speed of a droid over a type of terrain */
static unsigned intCalcSpeed(TYPE_OF_TERRAIN type, PropulsionStats* psProp)
{
	if (calcDroidWeight(&sCurrDesign) == 0)
	{
		return 0;
	}
	DroidTemplate psTempl = sCurrDesign;
	psTempl.asParts[COMPONENT_TYPE::PROPULSION] = getCompFromID(COMPONENT_TYPE::PROPULSION, psProp->id);
	unsigned weight = calcDroidWeight(&psTempl);
	if (weight == 0)
	{
		return 0;
	}
	//we want the design screen to show zero speed over water for all prop types except Hover and Vtol
	if (type == TER_WATER)
	{
		if (!(psProp->propulsionType == PROPULSION_TYPE::HOVER || psProp->propulsionType == PROPULSION_TYPE::LIFT))
		{
			return 0;
		}
	}
	unsigned droidSpeed = calcDroidSpeed(calcDroidBaseSpeed(&psTempl, weight, selectedPlayer), type,
	                                   psProp - asPropulsionStats, 0);
	return droidSpeed;
}


/* Set the bar graphs for the Propulsion stats */
static void intSetPropulsionStats(PropulsionStats* psStats)
{
	W_FORM* psForm;

	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid stats pointer");
	ASSERT_OR_RETURN(, psStats->hasType(STAT_PROPULSION), "stats have wrong type");

	/* set form tip to stats string */
	widgSetTip(psWScreen, IDDES_PROPFORM, checkIfZNullStat(psStats) ? "" : getStatsName(psStats));

	/* set form stats for later display in intDisplayStatForm */
	psForm = (W_FORM*)widgGetFromID(psWScreen, IDDES_PROPFORM);
	if (psForm != nullptr)
	{
		psForm->pUserData = psStats;
	}

	switch (desPropMode)
	{
	case IDES_GROUND:
		/* Road speed */
		widgSetBarSize(psWScreen, IDDES_PROPROAD, intCalcSpeed(TER_ROAD, psStats));
	/* Cross country speed - grass */
		widgSetBarSize(psWScreen, IDDES_PROPCOUNTRY, intCalcSpeed(TER_SANDYBRUSH, psStats));
	/* Water speed */
		widgSetBarSize(psWScreen, IDDES_PROPWATER, intCalcSpeed(TER_WATER, psStats));
		break;
	case IDES_AIR:
		/* Air speed - terrain type doesn't matter, use road */
		widgSetBarSize(psWScreen, IDDES_PROPAIR, intCalcSpeed(TER_ROAD, psStats));
		break;
	default:
		break;
	}

	widgSetBarSize(psWScreen, IDDES_PROPWEIGHT, calculatePropulsionWeight(*psStats));
}

static unsigned calculatePropulsionWeight(const ComponentStats& propulsionStats)
{
	if (sCurrDesign.asParts[COMPONENT_TYPE::BODY] == 0)
	{
		return 0;
	}

	return propulsionStats.weight * asBodyStats[sCurrDesign.asParts[COMPONENT_TYPE::BODY]].weight / 100;
}

/* Set the shadow bar graphs for the Propulsion stats */
static void intSetPropulsionShadowStats(PropulsionStats* psStats)
{
	ASSERT(psStats == nullptr || psStats->hasType(STAT_PROPULSION), "stats have wrong type");

	/* Only set the shadow stats if they are the right type */
	if (psStats &&
		((asPropulsionTypes[psStats->propulsionType].travel == TRAVEL_MEDIUM::GROUND &&
				desPropMode == IDES_AIR) ||
			(asPropulsionTypes[psStats->propulsionType].travel == TRAVEL_MEDIUM::AIR &&
				desPropMode == IDES_GROUND)))
	{
		// Reset the shadow bars. Prevent an incredibly trivial case where
		// hovering over a valid propulsion and then to an invalid one to compare
		// against (design is wheels, then hovered over half-tracks, then VTOL)
		// causes the last shadow marker set to stay.
		if (asPropulsionTypes[psStats->propulsionType].travel == GROUND && desPropMode == IDES_AIR)
		{
			widgSetMinorBarSize(psWScreen, IDDES_PROPAIR, 0);
		}
		else
		{
			widgSetMinorBarSize(psWScreen, IDDES_PROPROAD, 0);
			widgSetMinorBarSize(psWScreen, IDDES_PROPCOUNTRY, 0);
			widgSetMinorBarSize(psWScreen, IDDES_PROPWATER, 0);
		}
		if (sCurrDesign.asParts[COMPONENT_TYPE::BODY] != 0)
		{
			widgSetMinorBarSize(psWScreen, IDDES_PROPWEIGHT, calculatePropulsionWeight(*psStats));
		}
		return;
	}

	switch (desPropMode)
	{
	case IDES_GROUND:
		if (psStats)
		{
			/* Road speed */
			widgSetMinorBarSize(psWScreen, IDDES_PROPROAD,
			                    intCalcSpeed(TER_ROAD, psStats));
			/* Cross country speed - grass */
			widgSetMinorBarSize(psWScreen, IDDES_PROPCOUNTRY,
			                    intCalcSpeed(TER_SANDYBRUSH, psStats));
			/* Water speed */
			widgSetMinorBarSize(psWScreen, IDDES_PROPWATER,
			                    intCalcSpeed(TER_WATER, psStats));
		}
		else
		{
			/* Reset the shadow bars */
			widgSetMinorBarSize(psWScreen, IDDES_PROPROAD, 0);
			widgSetMinorBarSize(psWScreen, IDDES_PROPCOUNTRY, 0);
			widgSetMinorBarSize(psWScreen, IDDES_PROPWATER, 0);
		}
		break;
	case IDES_AIR:
		if (psStats)
		{
			/* Air speed - terrain type doesn't matter, use ROAD */
			widgSetMinorBarSize(psWScreen, IDDES_PROPAIR,
			                    intCalcSpeed(TER_ROAD, psStats));
		}
		else
		{
			/* Reset the shadow bar */
			widgSetMinorBarSize(psWScreen, IDDES_PROPAIR, 0);
		}
		break;
	default:
		break;
	}

	if (psStats)
	{
		widgSetMinorBarSize(psWScreen, IDDES_PROPWEIGHT, calculatePropulsionWeight(*psStats));
	}
	else
	{
		/* Reset the shadow bar */
		widgSetMinorBarSize(psWScreen, IDDES_PROPWEIGHT, 0);
	}
}

static constexpr auto ASSERT_PLAYER_OR_RETURN(retVal, = player) \;
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

/* Check whether a droid template is valid */
bool intValidTemplate(DroidTemplate* psTempl, const char* newName, bool complain, int player)
{
	ASSERT_PLAYER_OR_RETURN(false, player);

	code_part level = complain ? LOG_ERROR : LOG_NEVER;
	int bodysize = asBodyStats[psTempl->asParts[COMPONENT_TYPE::BODY]].size;

	// set the weapon for a command droid
	if (psTempl->asParts[COMPONENT_TYPE::BRAIN] != 0)
	{
		psTempl->weaponCount = 1;
		psTempl->asWeaps[0] = asBrainStats[psTempl->asParts[COMPONENT_TYPE::BRAIN]].psWeaponStat - asWeaponStats;
	}

	/* Check all the components have been set */
	if (psTempl->asParts[COMPONENT_TYPE::BODY] == 0)
	{
		debug(level, "No body given for template");
		return false;
	}
	else if (psTempl->asParts[COMPONENT_TYPE::PROPULSION] == 0)
	{
		debug(level, "No propulsion given for template");
		return false;
	}

	// Check a turret has been installed
	if (psTempl->weaponCount == 0 &&
		psTempl->asParts[COMPONENT_TYPE::SENSOR] == 0 &&
		psTempl->asParts[COMPONENT_TYPE::ECM] == 0 &&
		psTempl->asParts[COMPONENT_TYPE::BRAIN] == 0 &&
		psTempl->asParts[COMPONENT_TYPE::REPAIRUNIT] == 0 &&
		psTempl->asParts[COMPONENT_TYPE::CONSTRUCT] == 0 &&
      !isTransporter(psTempl))
	{
		debug(level, "No turret for template");
		return false;
	}

	// Check the weapons
	for (int i = 0; i < psTempl->weaponCount; i++)
	{
		int weaponSize = asWeaponStats[psTempl->asWeaps[i]].weaponSize;

		if ((weaponSize == WEAPON_SIZE_LIGHT && bodysize != SIZE_LIGHT)
			|| (weaponSize == WEAPON_SIZE_HEAVY && bodysize == SIZE_LIGHT)
			|| psTempl->asWeaps[i] == 0)
		{
			debug(level, "No weapon given for weapon droid, or wrong weapon size");
			return false;
		}
		if (checkTemplateIsVtol(psTempl)
			&& asWeaponStats[psTempl->asWeaps[i]].vtolAttackRuns <= 0)
		{
			debug(level, "VTOL with non-VTOL turret, not possible");
			return false;
		}
	}

	// Check number of weapon slots
	if ((unsigned)psTempl->weaponCount > asBodyStats[psTempl->asParts[COMPONENT_TYPE::BODY]].weaponSlots)
	{
		debug(level, "Too many weapon turrets");
		return false;
	}

	// Check no mixing of systems and weapons
	if (psTempl->weaponCount != 0 &&
      (psTempl->asParts[COMPONENT_TYPE::SENSOR] ||
			psTempl->asParts[COMPONENT_TYPE::ECM] ||
			(psTempl->asParts[COMPONENT_TYPE::REPAIRUNIT] && psTempl->asParts[COMPONENT_TYPE::REPAIRUNIT] != aDefaultRepair[player]) ||
			psTempl->asParts[COMPONENT_TYPE::CONSTRUCT]))
	{
		debug(level, "Cannot mix system and weapon turrets in a template!");
		return false;
	}
	if (psTempl->weaponCount != 1 && psTempl->asParts[COMPONENT_TYPE::BRAIN])
	{
		debug(level, "Commander template needs 1 weapon turret");
		return false;
	}

	//can only have a VTOL weapon on a VTOL propulsion
	if (checkTemplateIsVtol(psTempl) && !isTransporter(psTempl) && psTempl->weaponCount == 0)
	{
		debug(level, "VTOL with system turret, not possible");
		return false;
	}

	if (psTempl->asParts[COMPONENT_TYPE::SENSOR] == 0)
	{
		/* Set the default Sensor */
		psTempl->asParts[COMPONENT_TYPE::SENSOR] = aDefaultSensor[player];
	}

	if (psTempl->asParts[COMPONENT_TYPE::ECM] == 0)
	{
		/* Set the default ECM */
		psTempl->asParts[COMPONENT_TYPE::ECM] = aDefaultECM[player];
	}

	if (psTempl->asParts[COMPONENT_TYPE::REPAIRUNIT] == 0)
	{
		/* Set the default Repair */
		psTempl->asParts[COMPONENT_TYPE::REPAIRUNIT] = aDefaultRepair[player];
	}

	psTempl->ref = STAT_TEMPLATE;

	//set the droidtype
	psTempl->type = droidTemplateType(psTempl);

	psTempl->isEnabled = true;

	/* copy name into template */
	if (newName)
	{
		psTempl->name = WzString::fromUtf8(newName);
	}

	return true;
}

static void desCreateDefaultTemplate()
{
	/* set current design to default */
	sCurrDesign = sDefaultDesignTemplate;
	sCurrDesign.isStored = false;

	/* reset stats */
	intSetDesignStats(&sCurrDesign);
	widgDelete(psWScreen, IDDES_SYSTEMFORM);
	desSysMode = IDES_NOSYSTEM;
}

/* Remove the design widgets from the widget screen */
void intRemoveDesign()
{
	widgDelete(psWScreen, IDDES_POWERFORM);
	widgDelete(psWScreen, IDDES_NAMEBOX);
	widgDelete(psWScreen, IDDES_TEMPLBASE);
	widgDelete(psWScreen, IDDES_RIGHTBASE);

	widgDelete(psWScreen, IDDES_BODYFORM);
	widgDelete(psWScreen, IDDES_PROPFORM);
	widgDelete(psWScreen, IDDES_SYSTEMFORM);

	widgDelete(psWScreen, IDDES_FORM);
	widgDelete(psWScreen, IDDES_STATSFORM);

	resetDesignPauseState();
}

/* set flashing flag for button */
static void intSetButtonFlash(unsigned id, bool bFlash)
{
	WIDGET* psWidget = widgGetFromID(psWScreen, id);

	ASSERT_OR_RETURN(, psWidget->type == WIDG_BUTTON, "Not a button");

	psWidget->displayFunction = bFlash ? intDisplayButtonFlash : intDisplayButtonHilight;
}

/*
 * desTemplateNameCustomised
 *
 * Checks whether user has customised template name : template not
 * customised if not complete or if generated name same as current.
 */
static bool desTemplateNameCustomised(DroidTemplate* psTemplate)
{
	return psTemplate->type != DROID_DEFAULT &&
		strcmp(getStatsName(psTemplate), GetDefaultTemplateName(psTemplate)) != 0;
}

static DroidTemplate* templateFromButtonId(unsigned buttonId, bool allowBlankTemplate = false)
{
	unsigned minIndex = allowBlankTemplate ? 0 : 1;
	unsigned index = buttonId - IDDES_TEMPLSTART;

	if (index >= minIndex && index < apsTemplateList.size())
	{
		return apsTemplateList[index];
	}
	return nullptr;
}

/* Process return codes from the design screen */
void intProcessDesign(unsigned id)
{
	/* check template button pressed */
	if (id >= IDDES_TEMPLSTART && id <= IDDES_TEMPLEND)
	{
		/* if first template create blank design */
		if (id == IDDES_TEMPLSTART)
		{
			desCreateDefaultTemplate();

			aCurrName[0] = '\0';
			sCurrDesign.name = WzString();

			/* reveal body button */
			widgReveal(psWScreen, IDDES_BODYBUTTON);
			/* hide other component buttons */
			widgHide(psWScreen, IDDES_SYSTEMBUTTON);
			widgHide(psWScreen, IDDES_PROPBUTTON);
			widgHide(psWScreen, IDDES_WPABUTTON);
			widgHide(psWScreen, IDDES_WPBBUTTON);

			/* set button render routines to flash */
			intSetButtonFlash(IDDES_BODYBUTTON, true);
			intSetButtonFlash(IDDES_SYSTEMBUTTON, true);
			intSetButtonFlash(IDDES_PROPBUTTON, true);
			intSetButtonFlash(IDDES_WPABUTTON, true);
			intSetButtonFlash(IDDES_WPBBUTTON, true);

			if (bMultiPlayer)
			{
				widgHide(psWScreen, IDDES_STOREBUTTON);
			}
		}
		else
		{
			/* Find the template for the new button */
			DroidTemplate* psTempl = templateFromButtonId(id, true);

			ASSERT_OR_RETURN(, psTempl != nullptr, "template not found!");

			if (psTempl != nullptr)
			{
				/* Set the new template */
				sCurrDesign = *psTempl;
				sstrcpy(aCurrName, getStatsName(psTempl));

				/* reveal body/propulsion/turret component buttons */
				widgReveal(psWScreen, IDDES_BODYBUTTON);
				widgReveal(psWScreen, IDDES_PROPBUTTON);
				widgReveal(psWScreen, IDDES_SYSTEMBUTTON);
				/* hide extra turrets */
				widgHide(psWScreen, IDDES_WPABUTTON);
				widgHide(psWScreen, IDDES_WPBBUTTON);

				/* turn off button flashes */
				intSetButtonFlash(IDDES_BODYBUTTON, false);
				intSetButtonFlash(IDDES_SYSTEMBUTTON, false);
				intSetButtonFlash(IDDES_PROPBUTTON, false);
				intSetButtonFlash(IDDES_WPABUTTON, false);
				intSetButtonFlash(IDDES_WPBBUTTON, false);

				// reveal additional buttons
				if (psTempl->weaponCount >= 2)
				{
					widgReveal(psWScreen, IDDES_WPABUTTON);
				}
				else
				{
					intSetButtonFlash(IDDES_WPABUTTON, true);
				}
				if (psTempl->weaponCount == 3)
				{
					widgReveal(psWScreen, IDDES_WPBBUTTON);
				}
				else
				{
					intSetButtonFlash(IDDES_WPBBUTTON, true);
				}

				if (bMultiPlayer)
				{
					widgReveal(psWScreen, IDDES_STOREBUTTON);
					updateStoreButton(sCurrDesign.isStored);
				}
			}
		}

		/* reveal design form if not already on-screen */
		widgReveal(psWScreen, IDDES_FORM);

		/* Droid template button has been pressed - clear the old button */
		if (droidTemplID != 0)
		{
			widgSetButtonState(psWScreen, droidTemplID, 0);
		}

		intSetDesignStats(&sCurrDesign);

		/* show body stats only */
		widgReveal(psWScreen, IDDES_STATSFORM);
		widgReveal(psWScreen, IDDES_BODYFORM);
		widgHide(psWScreen, IDDES_PROPFORM);
		widgHide(psWScreen, IDDES_SYSTEMFORM);

		/*Update the Power bar stats as the power to build will have changed */
		intSetDesignPower(&sCurrDesign);
		/*Update the body points */
		intSetBodyPoints(&sCurrDesign);

		/* Lock the button */
		widgSetButtonState(psWScreen, id, WBUT_LOCK);
		droidTemplID = id;

		/* Update the component form */
		widgDelete(psWScreen, IDDES_RIGHTBASE);
		/* reset button states */
		widgSetButtonState(psWScreen, IDDES_SYSTEMBUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_BODYBUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_PROPBUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_WPABUTTON, 0);
		widgSetButtonState(psWScreen, IDDES_WPBBUTTON, 0);
		desCompMode = IDES_NOCOMPONENT;
		intSetDesignMode(IDES_BODY);
	}
	else if (id >= IDDES_COMPSTART && id <= IDDES_COMPEND)
	{
		/* check whether can change template name */
		bool bTemplateNameCustomised = desTemplateNameCustomised(&sCurrDesign);

		/* Component stats button has been pressed - clear the old button */
		if (desCompID != 0)
		{
			widgSetButtonState(psWScreen, desCompID, 0);
		}

		/* Set the stats in the template */
		switch (desCompMode)
		{
		case IDES_SYSTEM:
			//0 weapon for utility droid
			sCurrDesign.weaponCount = 0;
			break;
		case IDES_TURRET:
			setTemplateStat(&sCurrDesign, apsComponentList[id - IDDES_COMPSTART]);
		//Watermelon:weaponslots >= 2
			if (asBodyStats[sCurrDesign.asParts[COMPONENT_TYPE::BODY]].weaponSlots >= 2)
			{
				/* reveal turret_a button if hidden */
				widgReveal(psWScreen, IDDES_WPABUTTON);
			}
		/* Set the new stats on the display */
			intSetSystemForm(apsComponentList[id - IDDES_COMPSTART]);
		// Stop the button flashing
			intSetButtonFlash(IDDES_SYSTEMBUTTON, false);

			triggerEvent(TRIGGER_DESIGN_WEAPON);
			break;
		//Added cases for 2nd/3rd turret
		case IDES_TURRET_A:
			setTemplateStat(&sCurrDesign, apsComponentList[id - IDDES_COMPSTART]);
		//Watermelon:weaponSlots > 2
			if (asBodyStats[sCurrDesign.asParts[COMPONENT_TYPE::BODY]].weaponSlots > 2)
			{
				/* reveal turret_b button if hidden */
				widgReveal(psWScreen, IDDES_WPBBUTTON);
			}
		/* Set the new stats on the display */
			intSetSystemForm(apsComponentList[id - IDDES_COMPSTART]);
		// Stop the button flashing
			intSetButtonFlash(IDDES_WPABUTTON, false);

			triggerEvent(TRIGGER_DESIGN_WEAPON);
			break;
		case IDES_TURRET_B:
			setTemplateStat(&sCurrDesign, apsComponentList[id - IDDES_COMPSTART]);
		/* Set the new stats on the display */
			intSetSystemForm(apsComponentList[id - IDDES_COMPSTART]);
		// Stop the button flashing
			intSetButtonFlash(IDDES_WPBBUTTON, false);

			triggerEvent(TRIGGER_DESIGN_WEAPON);
			break;
		case IDES_BODY:
			{
				/* reveal propulsion button if hidden */
				widgReveal(psWScreen, IDDES_PROPBUTTON);

				setTemplateStat(&sCurrDesign, apsComponentList[id - IDDES_COMPSTART]);
				/* Set the new stats on the display */
				intSetBodyStats((BodyStats*)apsComponentList[id - IDDES_COMPSTART]);

				int numWeaps = sCurrDesign.asParts[COMPONENT_TYPE::BRAIN] != 0 ? 0 : sCurrDesign.weaponCount;
				int maxWeaps = asBodyStats[sCurrDesign.asParts[COMPONENT_TYPE::BODY]].weaponSlots;
				widgGetFromID(psWScreen, IDDES_WPABUTTON)->show(maxWeaps > 1 && numWeaps >= 1);
				widgGetFromID(psWScreen, IDDES_WPBBUTTON)->show(maxWeaps > 2 && numWeaps >= 2);
				widgSetButtonState(psWScreen, IDDES_WPABUTTON, maxWeaps > 1 && numWeaps == 1 ? WBUT_FLASH : 0);
				widgSetButtonState(psWScreen, IDDES_WPBBUTTON, maxWeaps > 2 && numWeaps == 2 ? WBUT_FLASH : 0);
				intSetButtonFlash(IDDES_WPABUTTON, maxWeaps > 1 && numWeaps == 1);
				intSetButtonFlash(IDDES_WPBBUTTON, maxWeaps > 2 && numWeaps == 2);
				// Stop the button flashing
				intSetButtonFlash(IDDES_BODYBUTTON, false);

				triggerEvent(TRIGGER_DESIGN_BODY);
				break;
			}
		case IDES_PROPULSION:
			setTemplateStat(&sCurrDesign, apsComponentList[id - IDDES_COMPSTART]);

		/* Set the new stats on the display */
			intSetPropulsionForm((PropulsionStats*)apsComponentList[id - IDDES_COMPSTART]);

		// Check that the weapon (if any) is valid for this propulsion
			if (!intCheckValidWeaponForProp(&sCurrDesign))
			{
				// Not valid weapon so initialise the weapon stat
				widgHide(psWScreen, IDDES_WPABUTTON);
				widgHide(psWScreen, IDDES_WPBBUTTON);

				// We need a turret again
				intSetButtonFlash(IDDES_SYSTEMBUTTON, true);
			}

		// Stop the button flashing
			intSetButtonFlash(IDDES_PROPBUTTON, false);

			triggerEvent(TRIGGER_DESIGN_PROPULSION);
			break;
		default:
			break;
		}

		/* Lock the new button */
		widgSetButtonState(psWScreen, id, WBUT_LOCK);
		desCompID = id;

		/* Update the propulsion stats as the droid weight will have changed */
		intSetPropulsionStats(asPropulsionStats + sCurrDesign.asParts[COMPONENT_TYPE::PROPULSION]);

		/*Update the Power bar stats as the power to build will have changed */
		intSetDesignPower(&sCurrDesign);
		/*Update the body points */
		intSetBodyPoints(&sCurrDesign);

		/* update name if not customised */
		if (!bTemplateNameCustomised)
		{
			sCurrDesign.name = WzString::fromUtf8(GetDefaultTemplateName(&sCurrDesign));
		}

		/* Update the name in the edit box */
		intSetEditBoxTextFromTemplate(&sCurrDesign);
	}
	else if (id >= IDDES_EXTRASYSSTART && id <= IDDES_EXTRASYSEND)
	{
		/* check whether can change template name */
		bool bTemplateNameCustomised = desTemplateNameCustomised(&sCurrDesign);

		// Extra component stats button has been pressed - clear the old button
		if (desCompID != 0)
		{
			widgSetButtonState(psWScreen, desCompID, 0);
		}

		// Now store the new stats
		setTemplateStat(&sCurrDesign, apsExtraSysList[id - IDDES_EXTRASYSSTART]);
		widgHide(psWScreen, IDDES_WPABUTTON);
		widgHide(psWScreen, IDDES_WPBBUTTON);
		// Set the new stats on the display
		intSetSystemForm(apsExtraSysList[id - IDDES_EXTRASYSSTART]);
		// Stop the button flashing
		intSetButtonFlash(IDDES_SYSTEMBUTTON, false);
		// Lock the new button
		widgSetButtonState(psWScreen, id, WBUT_LOCK);
		desCompID = id;

		// Update the propulsion stats as the droid weight will have changed
		intSetPropulsionStats(&asPropulsionStats[sCurrDesign.asParts[COMPONENT_TYPE::PROPULSION]]);

		// Update the Power bar stats as the power to build will have changed
		intSetDesignPower(&sCurrDesign);
		// Update the body points
		intSetBodyPoints(&sCurrDesign);

		/* update name if not customised */
		if (!bTemplateNameCustomised)
		{
			sCurrDesign.name = WzString::fromUtf8(GetDefaultTemplateName(&sCurrDesign));
		}

		/* Update the name in the edit box */
		intSetEditBoxTextFromTemplate(&sCurrDesign);

		if (apsExtraSysList[id - IDDES_EXTRASYSSTART]->compType == COMPONENT_TYPE::BRAIN)
		{
			triggerEvent(TRIGGER_DESIGN_COMMAND);
		}
		else
		{
			triggerEvent(TRIGGER_DESIGN_SYSTEM);
		}
	}
	else
	{
		switch (id)
		{
		/* The four component clickable forms */
		/* the six component clickable forms... */
		case IDDES_WEAPONS:
			desCompID = 0;
			intSetDesignMode(IDES_TURRET);
			break;
		case IDDES_WEAPONS_A:
			desCompID = 0;
			intSetDesignMode(IDES_TURRET_A);
			break;
		case IDDES_WEAPONS_B:
			desCompID = 0;
			intSetDesignMode(IDES_TURRET_B);
			break;
		case IDDES_COMMAND:
			desCompID = 0;
			break;
		case IDDES_SYSTEMS:
			desCompID = 0;
			intSetDesignMode(IDES_SYSTEM);
			break;
		/* The name edit box */
		case IDDES_NAMEBOX:
			sCurrDesign.name = widgGetWzString(psWScreen, IDDES_NAMEBOX);
			sstrcpy(aCurrName, getStatsName(&sCurrDesign));
			break;
		case IDDES_BIN:
			{
				/* Find the template for the current button */
				auto psTempl = templateFromButtonId(droidTemplID);
				// Does not return the first template, which is the empty template.

				/* remove template if found */
				if (psTempl != nullptr)
				{
					//update player template list.
					for (auto& i : localTemplates)
					{
						if (&i == psTempl)
						{
							//before deleting the template, need to make sure not being used in production
							deleteTemplateFromProduction(psTempl, selectedPlayer, ModeQueue);
							// Delete the template.
							std::erase(localTemplates, i);
							break;
						}
					}

					/* get previous template and set as current */
					psTempl = templateFromButtonId(droidTemplID - 1, true);
					// droidTemplID - 1 always valid (might be the first template), since droidTemplID is not the first template.
// see above comment
					ASSERT_OR_RETURN(, psTempl != nullptr, "template not found! - unexpected!");

					/* update local list */
					desSetupDesignTemplates();

					/* Now update the droid template form */
					widgDelete(psWScreen, IDDES_TEMPLBASE);
					intAddTemplateForm(psTempl);

					/* Set the new template */
					sCurrDesign = *psTempl;
					sstrcpy(aCurrName, getStatsName(psTempl));

					intSetEditBoxTextFromTemplate(psTempl);

					intSetDesignStats(&sCurrDesign);

					/* show body stats only */
					widgReveal(psWScreen, IDDES_STATSFORM);
					widgReveal(psWScreen, IDDES_BODYFORM);
					widgHide(psWScreen, IDDES_PROPFORM);
					widgHide(psWScreen, IDDES_SYSTEMFORM);

					/*Update the Power bar stats as the power to build will have changed */
					intSetDesignPower(&sCurrDesign);
					/*Update the body points */
					intSetBodyPoints(&sCurrDesign);

					/* show correct body component highlight */
					widgDelete(psWScreen, IDDES_RIGHTBASE);
					/* reset button states */
					widgSetButtonState(psWScreen, IDDES_SYSTEMBUTTON, 0);
					widgSetButtonState(psWScreen, IDDES_BODYBUTTON, 0);
					widgSetButtonState(psWScreen, IDDES_PROPBUTTON, 0);
					widgSetButtonState(psWScreen, IDDES_WPABUTTON, 0);
					widgSetButtonState(psWScreen, IDDES_WPBBUTTON, 0);
					desCompMode = IDES_NOCOMPONENT;
					intSetDesignMode(IDES_BODY);
				}
				break;
			}
		case IDDES_STOREBUTTON:
// Invert the current status
			sCurrDesign.isStored = !sCurrDesign.isStored;
			saveTemplate();
			storeTemplates();
			updateStoreButton(sCurrDesign.isStored);
			break;
		case IDDES_SYSTEMBUTTON:
			// Add the correct component form
			switch (droidTemplateType(&sCurrDesign))
			{
			case DROID_COMMAND:
			case DROID_SENSOR:
			case DROID_CONSTRUCT:
			case DROID_ECM:
			case DROID_REPAIR:
				intSetDesignMode(IDES_SYSTEM);
				break;
			default:
				intSetDesignMode(IDES_TURRET);
				break;
			}
		/* reveal components if not already onscreen */
			widgReveal(psWScreen, IDDES_STATSFORM);
			widgReveal(psWScreen, IDDES_RIGHTBASE);
			widgReveal(psWScreen, IDDES_SYSTEMFORM);
			widgHide(psWScreen, IDDES_BODYFORM);
			widgHide(psWScreen, IDDES_PROPFORM);

			break;
		// WPABUTTON
		case IDDES_WPABUTTON:
			// Add the correct component form
			switch (droidTemplateType(&sCurrDesign))
			{
			case DROID_COMMAND:
			case DROID_SENSOR:
			case DROID_CONSTRUCT:
			case DROID_ECM:
			case DROID_REPAIR:
				break;
			default:
				intSetDesignMode(IDES_TURRET_A);
				break;
			}
		/* reveal components if not already onscreen */
			widgReveal(psWScreen, IDDES_STATSFORM);
			widgReveal(psWScreen, IDDES_RIGHTBASE);
			widgReveal(psWScreen, IDDES_SYSTEMFORM);
			widgHide(psWScreen, IDDES_BODYFORM);
			widgHide(psWScreen, IDDES_PROPFORM);

			break;
		// WPBBUTTON
		case IDDES_WPBBUTTON:
			// Add the correct component form
			switch (droidTemplateType(&sCurrDesign))
			{
			case DROID_COMMAND:
			case DROID_SENSOR:
			case DROID_CONSTRUCT:
			case DROID_ECM:
			case DROID_REPAIR:
				break;
			default:
				intSetDesignMode(IDES_TURRET_B);
				break;
			}
		/* reveal components if not already onscreen */
			widgReveal(psWScreen, IDDES_STATSFORM);
			widgReveal(psWScreen, IDDES_RIGHTBASE);
			widgReveal(psWScreen, IDDES_SYSTEMFORM);
			widgHide(psWScreen, IDDES_BODYFORM);
			widgHide(psWScreen, IDDES_PROPFORM);

			break;
		case IDDES_BODYBUTTON:
			/* reveal components if not already onscreen */
			widgReveal(psWScreen, IDDES_RIGHTBASE);
			intSetDesignMode(IDES_BODY);

			widgReveal(psWScreen, IDDES_STATSFORM);
			widgHide(psWScreen, IDDES_SYSTEMFORM);
			widgReveal(psWScreen, IDDES_BODYFORM);
			widgHide(psWScreen, IDDES_PROPFORM);

			break;
		case IDDES_PROPBUTTON:
			/* reveal components if not already onscreen */
			widgReveal(psWScreen, IDDES_RIGHTBASE);
			intSetDesignMode(IDES_PROPULSION);
			widgReveal(psWScreen, IDDES_STATSFORM);
			widgHide(psWScreen, IDDES_SYSTEMFORM);
			widgHide(psWScreen, IDDES_BODYFORM);
			widgReveal(psWScreen, IDDES_PROPFORM);

			break;
		case IDSTAT_OBSOLETE_BUTTON:
			includeRedundantDesigns = !includeRedundantDesigns;
			auto obsoleteButton = (MultipleChoiceButton*)widgGetFromID(psWScreen, IDSTAT_OBSOLETE_BUTTON);
			obsoleteButton->setChoice(includeRedundantDesigns);
		// Refresh lists.
			if (droidTemplID != IDDES_TEMPLSTART)
			{
				intRemoveDesign();
				intAddDesign(false);
			}
			else
			{
				desSetupDesignTemplates();
				widgDelete(psWScreen, IDDES_TEMPLBASE);
				intAddTemplateForm(templateFromButtonId(droidTemplID));
				intSetDesignMode(desCompMode, true);
				droidTemplID = IDDES_TEMPLSTART;
				widgSetButtonState(psWScreen, droidTemplID, WBUT_LOCK);
			}
			break;
		}
	}

	/* show body button if component button pressed and
	 * save template if valid
	 */
	if ((id >= IDDES_COMPSTART && id <= IDDES_COMPEND) || (id >= IDDES_EXTRASYSSTART && id <= IDDES_EXTRASYSEND))
	{
		/* reveal body button if hidden */
		widgReveal(psWScreen, IDDES_BODYBUTTON);

		/* save template if valid */
		if (saveTemplate())
		{
			triggerEventDesignCreated(&sCurrDesign);
		}

		switch (desCompMode)
		{
		case IDES_BODY:
			widgReveal(psWScreen, IDDES_BODYFORM);
			widgHide(psWScreen, IDDES_PROPFORM);
			widgHide(psWScreen, IDDES_SYSTEMFORM);
			break;

		case IDES_PROPULSION:
			widgHide(psWScreen, IDDES_BODYFORM);
			widgReveal(psWScreen, IDDES_PROPFORM);
			widgHide(psWScreen, IDDES_SYSTEMFORM);
			break;

		case IDES_SYSTEM:
		case IDES_TURRET:
		// reveals SYSTEMFORM
		case IDES_TURRET_A:
		case IDES_TURRET_B:
			widgHide(psWScreen, IDDES_BODYFORM);
			widgHide(psWScreen, IDDES_PROPFORM);
			widgReveal(psWScreen, IDDES_SYSTEMFORM);
			break;
		default:
			break;
		}

		widgReveal(psWScreen, IDDES_STATSFORM);

		/* switch automatically to next component type if initial design */
		if (!intValidTemplate(&sCurrDesign, aCurrName, false, selectedPlayer))
		{
			/* show next component design screen */
			switch (desCompMode)
			{
			case IDES_BODY:
				intSetDesignMode(IDES_PROPULSION);
				widgReveal(psWScreen, IDDES_PROPBUTTON);
				break;

			case IDES_PROPULSION:
				intSetDesignMode(IDES_TURRET);
				widgReveal(psWScreen, IDDES_SYSTEMBUTTON);
				break;

			case IDES_SYSTEM:
			case IDES_TURRET:
				if ((asBodyStats + sCurrDesign.asParts[COMPONENT_TYPE::BODY])->weapon_slots > 1 &&
            sCurrDesign.weaponCount == 1 && sCurrDesign.asParts[COMPONENT_TYPE::BRAIN] == 0)
				{
					debug(LOG_GUI, "intProcessDesign: First weapon selected, doing next.");
					intSetDesignMode(IDES_TURRET_A);
					widgReveal(psWScreen, IDDES_WPABUTTON);
				}
				else
				{
					debug(LOG_GUI, "intProcessDesign: First weapon selected, is final.");
				}
				break;
			case IDES_TURRET_A:
				if ((asBodyStats + sCurrDesign.asParts[COMPONENT_TYPE::BODY])->weapon_slots > 2)
				{
					debug(LOG_GUI, "intProcessDesign: Second weapon selected, doing next.");
					intSetDesignMode(IDES_TURRET_B);
					widgReveal(psWScreen, IDDES_WPBBUTTON);
				}
				else
				{
					debug(LOG_GUI, "intProcessDesign: Second weapon selected, is final.");
				}
				break;
			case IDES_TURRET_B:
				debug(LOG_GUI, "intProcessDesign: Third weapon selected, is final.");
				break;
			default:
				break;
			}
		}
	}
	//save the template if the name gets edited
	if (id == IDDES_NAMEBOX)
	{
		saveTemplate();
	}
}


/* Set the shadow bar graphs for the design screen */
void intRunDesign()
{
	ComponentStats* psStats;

	/* Find out which button was hilited */
	bool templateButton = false;
	unsigned statID = widgGetMouseOver(psWScreen);

	// Somut around here is casuing a nasty crash.....
	/* If a component button is hilited get the stats for it */
	if (statID == desCompID)
	{
		/* The mouse is over the selected component - no shadow stats */
		psStats = nullptr;
	}
	else if (statID >= IDDES_COMPSTART && statID <= IDDES_COMPEND)
	{
		unsigned compIndex = statID - IDDES_COMPSTART;
		ASSERT_OR_RETURN(, compIndex < numComponent, "Invalid range referenced for numComponent, %d > %d", compIndex,
		                   numComponent);
		psStats = apsComponentList[compIndex];
	}
	else if (statID >= IDDES_EXTRASYSSTART && statID <= IDDES_EXTRASYSEND)
	{
		unsigned compIndex = statID - IDDES_EXTRASYSSTART;
		ASSERT_OR_RETURN(, compIndex < numExtraSys, "Invalid range referenced for numExtraSys, %d > %d", compIndex,
		                   numExtraSys);
		psStats = apsExtraSysList[compIndex];
	}
	else if (statID >= IDDES_TEMPLSTART && statID <= IDDES_TEMPLEND)
	{
		runTemplateShadowStats(statID);
		templateButton = true;
		psStats = nullptr;
	}
	else
	{
		/* No component button so reset the stats to nothing */
		psStats = nullptr;
	}

	/* Now set the bar graphs for the stats - don't bother if over template
	since setting them all!*/
	if (!templateButton)
	{
		switch (desCompMode)
		{
		case IDES_SYSTEM:
		case IDES_TURRET:
		case IDES_TURRET_A:
		case IDES_TURRET_B:
			intSetBodyShadowStats(nullptr);
			intSetPropulsionShadowStats(nullptr);
			intSetSystemShadowStats(psStats);
			break;
		case IDES_BODY:
			intSetSystemShadowStats(nullptr);
			intSetPropulsionShadowStats(nullptr);
			intSetBodyShadowStats((BodyStats*)psStats);
			break;
		case IDES_PROPULSION:
			intSetSystemShadowStats(nullptr);
			intSetBodyShadowStats(nullptr);
			intSetPropulsionShadowStats((PropulsionStats*)psStats);
			break;
		default:
			break;
		}

		//set the template shadow stats
		intSetTemplateBodyShadowStats(psStats);
		intSetTemplatePowerShadowStats(psStats);
	}

	if (keyPressed(KEY_ESC))
	{
		intResetScreen(false);
		// clear key press so we don't enter in-game options
		inputLoseFocus();
	}
}

static void intDisplayStatForm(WIDGET* psWidget, unsigned xOffset, unsigned yOffset)
{
	static unsigned iRY = 45;
	auto Form = (W_CLICKFORM*)psWidget;
	UWORD x0 = xOffset + Form->x(), y0 = yOffset + Form->y();

	/* get stats from userdata pointer in widget stored in
	 * intSetSystemStats, intSetBodyStats, intSetPropulsionStats
	 */
	auto psStats = (BaseStats*)Form->pUserData;

	SWORD templateRadius = getComponentRadius(psStats);

	Vector3i Rotation(-30, iRY, 0), Position(0, -templateRadius / 4, BUTTON_DEPTH /* templateRadius * 12 */);

	//scale the object around the BUTTON_RADIUS so that half size objects are draw are draw 75% the size of normal objects
	SDWORD falseScale = (DESIGN_COMPONENT_SCALE * COMPONENT_RADIUS) / templateRadius / 2 + (DESIGN_COMPONENT_SCALE / 2);

	iV_DrawImage(IntImages, (UWORD)(IMAGE_DES_STATBACKLEFT), x0, y0);
	iV_DrawImageRepeatX(IntImages, IMAGE_DES_STATBACKMID, x0 + iV_GetImageWidth(IntImages, IMAGE_DES_STATBACKLEFT), y0,
	                    Form->width() - iV_GetImageWidth(IntImages, IMAGE_DES_STATBACKLEFT) - iV_GetImageWidth(
		                    IntImages, IMAGE_DES_STATBACKRIGHT), defaultProjectionMatrix(), true);
	iV_DrawImage(IntImages, IMAGE_DES_STATBACKRIGHT,
	             x0 + Form->width() - iV_GetImageWidth(IntImages, IMAGE_DES_STATBACKRIGHT), y0);

	/* display current component */
	pie_SetGeometricOffset(xOffset + psWidget->width() / 4, yOffset + psWidget->height() / 2);

	/* inc rotation if highlighted */
	if ((Form->getState() & WBUT_HIGHLIGHT) != 0)
	{
		iRY += realTimeAdjustedAverage(BUTTONOBJ_ROTSPEED);
		iRY %= 360;
	}

	//display component in bottom design screen window
	displayComponentButton(psStats, &Rotation, &Position, falseScale);
}

/* Displays the 3D view of the droid in a window on the design form */
static void intDisplayViewForm(WIDGET* psWidget, unsigned xOffset, unsigned yOffset)
{
	auto* Form = (W_FORM*)psWidget;
	static unsigned iRY = 45;

	int x0 = xOffset + Form->x();
	int y0 = yOffset + Form->y();
	int x1 = x0 + Form->width();
	int y1 = y0 + Form->height();

	RenderWindowFrame(FRAME_NORMAL, x0, y0, x1 - x0, y1 - y0);

	pie_SetGeometricOffset((DES_CENTERFORMX + DES_3DVIEWX) + (DES_3DVIEWWIDTH / 2),
	                       (DES_CENTERFORMY + DES_3DVIEWY) + (DES_3DVIEWHEIGHT / 4) + 32);

	Vector3i Rotation = {-30, iRY, 0};

	/* inc rotation */
	iRY += realTimeAdjustedAverage(BUTTONOBJ_ROTSPEED);
	iRY %= 360;

	//fixed depth scale the pie
	Vector3i Position = {0, -100, BUTTON_DEPTH};

	int templateRadius = getComponentDroidTemplateRadius(&sCurrDesign);
	//scale the object around the OBJECT_RADIUS so that half size objects are draw are draw 75% the size of normal objects
	int falseScale = (DESIGN_DROID_SCALE * OBJECT_RADIUS) / templateRadius;

	//display large droid view in the design screen
	displayComponentButtonTemplate(&sCurrDesign, &Rotation, &Position, falseScale);
}

/* General display window for the design form SOLID BACKGROUND - NOT TRANSPARENT*/
static void intDisplayDesignForm(WIDGET* psWidget, unsigned xOffset, unsigned yOffset)
{
	int x0 = xOffset + psWidget->x();
	int y0 = yOffset + psWidget->y();
	int x1 = x0 + psWidget->width();
	int y1 = y0 + psWidget->height();

	RenderWindowFrame(FRAME_NORMAL, x0, y0, x1 - x0, y1 - y0);
}


/* save the current Template if valid. Return true if stored */
static bool saveTemplate()
{
	if (!intValidTemplate(&sCurrDesign, aCurrName, false, selectedPlayer))
	{
		if (bMultiPlayer)
		{
			widgHide(psWScreen, IDDES_STOREBUTTON);
		}
		return false;
	}
	if (bMultiPlayer)
	{
		widgReveal(psWScreen, IDDES_STOREBUTTON);
// Change the buttons icon
		updateStoreButton(sCurrDesign.isStored);
	}

	/* if first (New Design) button selected find empty template
	 * else find current button template
	 */
	DroidTemplate* psTempl;
	if (droidTemplID == IDDES_TEMPLSTART)
	{
		/* create empty template and point to that */
		localTemplates.emplace_back();
		psTempl = &localTemplates.back();
		sCurrDesign.id = generateNewObjectId();
		apsTemplateList.push_back(psTempl);

		psTempl->ref = STAT_TEMPLATE;

		/* set button render routines to highlight, not flash */
		intSetButtonFlash(IDDES_SYSTEMBUTTON, false);
		intSetButtonFlash(IDDES_BODYBUTTON, false);
		intSetButtonFlash(IDDES_PROPBUTTON, false);
	}
	else
	{
		/* Find the template for the current button */
		psTempl = templateFromButtonId(droidTemplID);
		if (psTempl == nullptr)
		{
			debug(LOG_ERROR, "Template not found for button");
			return false;
		}

		// ANY change to the template affect the production - even if the template is changed and then changed back again!
		deleteTemplateFromProduction(psTempl, selectedPlayer, ModeQueue);
	}

	/* Copy the template */
	*psTempl = sCurrDesign;

	/* Now update the droid template form */
	widgDelete(psWScreen, IDDES_TEMPLBASE);
	intAddTemplateForm(psTempl);

	// Add template to in-game template list, since localTemplates/apsTemplateList is for UI use only.
	copyTemplate(selectedPlayer, psTempl);

	return true;
}


/* Function to set the shadow bars for all the stats when the mouse is over the Template buttons */
void runTemplateShadowStats(unsigned id)
{
	/* Find the template for the new button */
	//we're ignoring the Blank Design so start at the second button
	DroidTemplate* psTempl = templateFromButtonId(id);

	//if we're over a different template
	if (psTempl && psTempl != &sCurrDesign)
	{
		/* Now set the bar graphs for the stats */
		intSetBodyShadowStats(asBodyStats + psTempl->asParts[COMPONENT_TYPE::BODY]);
		intSetPropulsionShadowStats(asPropulsionStats + psTempl->asParts[COMPONENT_TYPE::PROPULSION]);
		//only set the system shadow bar if the same type of droid
		ComponentStats* psStats = nullptr;
		DROID_TYPE templType = droidTemplateType(psTempl);
		if (templType == droidTemplateType(&sCurrDesign))
		{
			unsigned compIndex;
			switch (templType)
			{
			case DROID_WEAPON:
				compIndex = psTempl->asWeaps[0];
				ASSERT_OR_RETURN(, compIndex < numWeaponStats, "Invalid range referenced for numWeaponStats, %d > %d",
				                   compIndex, numWeaponStats);
				psStats = &asWeaponStats[compIndex];
				break;
			case DROID_SENSOR:
				compIndex = psTempl->asParts[COMPONENT_TYPE::SENSOR];
				ASSERT_OR_RETURN(, compIndex < numSensorStats, "Invalid range referenced for numSensorStats, %d > %d",
				                   compIndex, numSensorStats);
				psStats = &asSensorStats[compIndex];
				break;
			case DROID_ECM:
				compIndex = psTempl->asParts[COMPONENT_TYPE::ECM];
				ASSERT_OR_RETURN(, compIndex < numECMStats, "Invalid range referenced for numECMStats, %d > %d",
				                   compIndex, numECMStats);
				psStats = &asECMStats[compIndex];
				break;
			case DROID_CONSTRUCT:
				compIndex = psTempl->asParts[COMPONENT_TYPE::CONSTRUCT];
				ASSERT_OR_RETURN(, compIndex < numConstructStats,
				                   "Invalid range referenced for numConstructStats, %d > %d", compIndex,
				                   numConstructStats);
				psStats = &asConstructStats[compIndex];
				break;
			case DROID_REPAIR:
				compIndex = psTempl->asParts[COMPONENT_TYPE::REPAIRUNIT];
				ASSERT_OR_RETURN(, compIndex < numRepairStats, "Invalid range referenced for numRepairStats, %d > %d",
				                   compIndex, numRepairStats);
				psStats = &asRepairStats[compIndex];
				break;
			default:
				break;
			}
		}

		if (psStats)
		{
			intSetSystemShadowStats(psStats);
		}
		//haven't got a stat so just do the code required here...
		widgSetMinorBarSize(psWScreen, IDDES_BODYPOINTS, calcTemplateBody(psTempl, selectedPlayer));
		widgSetMinorBarSize(psWScreen, IDDES_POWERBAR, calcTemplatePower(psTempl));
	}
}

/*sets which states need to be paused when the design screen is up*/
static void setDesignPauseState()
{
	if (!bMultiPlayer && !bInTutorial)
	{
		//need to clear mission widgets from being shown on design screen
		clearMissionWidgets();
		gameTimeStop();
		setGameUpdatePause(true);
		setScrollPause(true);
		screen_RestartBackDrop();
	}
}

/*resets the pause states */
static void resetDesignPauseState()
{
	if (!bMultiPlayer && !bInTutorial)
	{
		//put any widgets back on for the missions
		resetMissionWidgets();
		setGameUpdatePause(false);
		setScrollPause(false);
		gameTimeStart();
		screen_StopBackDrop();
	}
}

/*this is called when a new propulsion type is added to the current design
to check the weapon is 'allowed'. Check if VTOL, the weapon is direct fire.
Also check numVTOLattackRuns for the weapon is not zero - return true if valid weapon*/
static bool intCheckValidWeaponForProp(DroidTemplate* psTemplate)
{
	if (asPropulsionTypes[asPropulsionStats[psTemplate->asParts[COMPONENT_TYPE::PROPULSION]].propulsionType].travel != AIR)
	{
		if (psTemplate->weaponCount == 0 &&
        (psTemplate->asParts[COMPONENT_TYPE::SENSOR] ||
				psTemplate->asParts[COMPONENT_TYPE::REPAIRUNIT] ||
				psTemplate->asParts[COMPONENT_TYPE::CONSTRUCT] ||
				psTemplate->asParts[COMPONENT_TYPE::ECM]))
		{
			// non-AIR propulsions can have systems, too.
			return true;
		}
	}
	return checkValidWeaponForProp(psTemplate);
}

//checks if the template has PROPULSION_TYPE::LIFT propulsion attached - returns true if it does
bool checkTemplateIsVtol(const DroidTemplate* psTemplate)
{
	return asPropulsionStats[psTemplate->asParts[COMPONENT_TYPE::PROPULSION]].propulsionType == PROPULSION_TYPE::LIFT;
}

void updateStoreButton(bool isStored)
{
	unsigned imageset;

	if (isStored)
	{
		imageset = PACKDWORD_TRI(0, IMAGE_DES_DELETEH, IMAGE_DES_DELETE);
		widgGetFromID(psWScreen, IDDES_STOREBUTTON)->setTip(_("Do Not Store Design"));
	}
	else
	{
		imageset = PACKDWORD_TRI(0, IMAGE_DES_SAVEH, IMAGE_DES_SAVE);
		widgGetFromID(psWScreen, IDDES_STOREBUTTON)->setTip(_("Store Design"));
	}

	widgSetUserData2(psWScreen, IDDES_STOREBUTTON, imageset);
}
