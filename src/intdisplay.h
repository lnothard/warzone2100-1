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

#ifndef __INCLUDED_SRC_INTDISPLAY_H__
#define __INCLUDED_SRC_INTDISPLAY_H__

#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/bar.h"

#include "droid.h"
#include "feature.h"
#include "research.h"
#include "structure.h"

/* Power levels are divided by this for power bar display. The extra factor has
been included so that the levels appear the same for the power bar as for the
power values in the buttons */
#define POWERBAR_SCALE			(5 * WBAR_SCALE/STAT_PROGBARWIDTH)

#define BUTTONOBJ_ROTSPEED		90	// Speed to rotate objects rendered in
// buttons ( degrees per second )

enum ImdType
{
	IMDTYPE_NONE,
	IMDTYPE_DROID,
	IMDTYPE_DROIDTEMPLATE,
	IMDTYPE_COMPONENT,
	IMDTYPE_STRUCTURE,
	IMDTYPE_RESEARCH,
	IMDTYPE_STRUCTURESTAT,
	IMDTYPE_FEATURE,
};

struct ImdObject
{
	ImdObject() : ptr(nullptr), type(IMDTYPE_NONE)
	{
	}

	static ImdObject Droid(BaseObject * p)
	{
		return {p, IMDTYPE_DROID};
	}

	static ImdObject DroidTemplate(BaseStats* p)
	{
		return {p, IMDTYPE_DROIDTEMPLATE};
	}

	static ImdObject Component(BaseStats* p)
	{
		return {p, IMDTYPE_COMPONENT};
	}

	static ImdObject Structure(BaseObject * p)
	{
		return {p, IMDTYPE_STRUCTURE};
	}

	static ImdObject Research(BaseStats* p)
	{
		return {p, IMDTYPE_RESEARCH};
	}

	static ImdObject StructureStat(BaseStats* p)
	{
		return {p, IMDTYPE_STRUCTURESTAT};
	}

	static ImdObject Feature(BaseStats* p)
	{
		auto* fStat = (FeatureStats*)p;
		return {&fStat->psImd, IMDTYPE_FEATURE};
	}

	[[nodiscard]] bool empty() const
	{
		return ptr == nullptr;
	}

	void* ptr;
	ImdType type;

private:
	ImdObject(void* ptr, ImdType type) : ptr(ptr), type(type)
	{
	}
};

// Set audio IDs for form opening/closing anims.
void SetFormAudioIDs(int OpenID, int CloseID);

// Initialise interface graphics.
bool intInitialiseGraphics();

class PowerBar : public W_BARGRAPH
{
public:
	explicit PowerBar(W_BARINIT* init): W_BARGRAPH(init)
	{
	}

	std::string getTip() override;
	void display(int xOffset, int yOffset) override;

private:
	struct DisplayPowerBarCache
	{
		WzText wzText;
		WzText wzNeedText;
	} cache;
};

class IntFancyButton : public W_CLICKFORM
{
public:
	IntFancyButton();

protected:
	//the two types of button used in the object display (bottom bar)
	enum ButtonType { TOPBUTTON = 0, BTMBUTTON = 1 };

	void initDisplay();
	void displayClear(int xOffset, int yOffset);
	void displayIMD(Image image, ImdObject imdObject, int xOffset, int yOffset);
	void displayImage(Image image, int xOffset, int yOffset);
	void displayBlank(int xOffset, int yOffset);
	void displayIfHighlight(int xOffset, int yOffset);

	struct
	{
		Vector3i position;
		Vector3i rotation;
		int scale;
		int rate;
	} model;

	ButtonType buttonType; // TOPBUTTON is square, BTMBUTTON has a little up arrow.
};

class IntObjectButton : public IntFancyButton
{
public:
	IntObjectButton();

	void display(int xOffset, int yOffset) override;

	virtual void setObject(BaseObject* object) {
		psObj = object;
	}

	bool clearData() {
		bool ret = psObj != nullptr;
		psObj = nullptr;
		return ret;
	}
protected:
  BaseObject* psObj;
};

class IntStatusButton : public IntObjectButton
{
public:
	IntStatusButton();

	void setObject(BaseObject* object) override
	{
		psObj = object;
		theStats = nullptr;
	}

	void setObjectAndStats(BaseObject* object, BaseStats* stats)
	{
		psObj = object;
		theStats = stats;
	}

	void display(int xOffset, int yOffset) override;
protected:
	BaseStats* theStats;
};

class IntStatsButton : public IntFancyButton
{
public:
	IntStatsButton();

	void display(int xOffset, int yOffset) override;

	void setStats(BaseStats* stats)
	{
		Stat = stats;
	}

	void setStatsAndTip(BaseStats* stats)
	{
		setStats(stats);
		setTip(getStatsName(stats));
	}
protected:
	BaseStats* Stat;
};

/// Form which only acts as a glass container.
class IntFormTransparent : public W_FORM
{
public:
	IntFormTransparent();

	void display(int xOffset, int yOffset) override;
};

/// Form which animates opening/closing.
class IntFormAnimated : public W_FORM
{
public:
	explicit IntFormAnimated(bool openAnimate = true);

	void display(int xOffset, int yOffset) override;

	void closeAnimateDelete(); ///< Animates the form closing, and deletes itself when done.
	bool isClosing() const;

private:
	unsigned startTime; ///< Animation start time
	int currentAction; ///< Opening/open/closing/closed.
};

void intDisplayImage(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void intDisplayImageHilight(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void intDisplayButtonHilight(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void intDisplayButtonFlash(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void intDisplayEditBox(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void formatTime(W_BARGRAPH* barGraph, int buildPointsDone, int buildPointsTotal, int buildRate, char const* toolTip);
void formatPower(W_BARGRAPH* barGraph, int neededPower, int powerToBuild);

bool DroidIsBuilding(Droid* Droid);
Structure* DroidGetBuildStructure(Droid const* Droid);
bool DroidGoingToBuild(Droid* Droid);
BaseStats* DroidGetBuildStats(Droid* Droid);
iIMDShape* DroidGetIMD(Droid* Droid);

bool StructureIsManufacturingPending(Structure* structure);
///< Returns true iff the structure is either manufacturing or on hold (even if not yet synchronised). (But ignores research.)
bool structureIsResearchingPending(Structure* structure);
///< Returns true iff the structure is either researching or on hold (even if not yet synchronised). (But ignores manufacturing.)
bool StructureIsOnHoldPending(Structure* structure);
///< Returns true iff the structure is on hold (even if not yet synchronised).
DroidTemplate* FactoryGetTemplate(Factory* Factory);

ResearchFacility* StructureGetResearch(Structure* Structure);
Factory* StructureGetFactory(Structure* Structure);

bool StatIsStructure(BaseStats const* Stat);
iIMDShape* StatGetStructureIMD(BaseStats* Stat, UDWORD Player);
bool StatIsTemplate(BaseStats* Stat);
bool StatIsFeature(BaseStats const* Stat);

COMPONENT_TYPE StatIsComponent(BaseStats const* Stat);
bool StatGetComponentIMD(BaseStats* Stat, COMPONENT_TYPE compID, iIMDShape** CompIMD, iIMDShape** MountIMD);

bool StatIsResearch(BaseStats* Stat);

// Widget callback function to play an audio track.
void WidgetAudioCallback(int AudioID);

class IntTransportButton : public IntFancyButton
{
public:
	IntTransportButton();

	void display(int xOffset, int yOffset) override;

	void setObject(Droid* object)
	{
		psDroid = object;
	}

protected:
	Droid* psDroid;
};

/*draws blips on radar to represent Proximity Display*/
void drawRadarBlips(int radarX, int radarY, float pixSizeH, float pixSizeV, const glm::mat4& modelViewProjection);

/*Displays the proximity messages blips over the world*/
void intDisplayProximityBlips(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void intUpdateQuantitySlider(WIDGET* psWidget, W_CONTEXT* psContext);

void intDisplayMissionClock(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset);

void intDisplayUpdateAllyBar(W_BARGRAPH* psBar, const ResearchStats& research, const std::vector<AllyResearch>& researches);
Structure* droidGetCommandFactory(Droid const* psDroid);

void intSetShadowPower(int quantity);

#endif // __INCLUDED_SRC_INTDISPLAY_H__
