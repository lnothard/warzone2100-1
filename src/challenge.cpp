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
 * @file challenge.cpp
 * Run challenges dialog
 */

#include <string>

#include "lib/ivis_opengl/piepalette.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/button.h"
#include "lib/widget/form.h"
#include "lib/widget/widgbase.h"
#include "titleui/multiplayer.h"

#include "challenge.h"
#include "hci.h"
#include "intdisplay.h"
#include "intfac.h"
#include "loadsave.h"
#include "mission.h"
#include "multiplay.h"

struct IMAGEFILE;
IMAGEFILE* IntImages;
tm getUtcTime(std::time_t const&);
void inputLoseFocus();
unsigned short iV_GetImageHeight(const IMAGEFILE*, unsigned short);
unsigned short iV_GetImageWidth(const IMAGEFILE*, unsigned short);
void pie_BoxFill(int, int, int, int, PIELIGHT);
bool WZ_PHYSFS_enumerateFiles(const char*, const std::function<bool (char*)>&);


static constexpr auto totalslots  = 36; 			// challenge slots
static constexpr auto slotsInColumn  = 12; 		// # of slots in a column
static constexpr auto totalslotspace  = 256; 		// max chars for slot strings.

static const auto CHALLENGE_X = D_W + 16; 
static const auto CHALLENGE_Y = D_H + 5;
static constexpr auto CHALLENGE_W = 610; 
static constexpr auto CHALLENGE_H = 215; 

static constexpr auto CHALLENGE_HGAP = 9; 
static constexpr auto CHALLENGE_VGAP = 9; 
static constexpr auto CHALLENGE_BANNER_DEPTH = 40;  		//top banner which displays either load or save

static constexpr auto CHALLENGE_ENTRY_W				= ((CHALLENGE_W / 3 )-(3 * CHALLENGE_HGAP));
static constexpr auto CHALLENGE_ENTRY_H			=	(CHALLENGE_H -(5 * CHALLENGE_VGAP )- (CHALLENGE_BANNER_DEPTH+CHALLENGE_VGAP) ) / 5;

static constexpr auto ID_LOADSAVE				= 21000;
static constexpr auto CHALLENGE_FORM			= ID_LOADSAVE+1;		// back form.
static constexpr auto CHALLENGE_CANCEL			= ID_LOADSAVE+2;		// cancel but.
static constexpr auto CHALLENGE_LABEL			= ID_LOADSAVE+3;	// load/save
static constexpr auto CHALLENGE_BANNER			= ID_LOADSAVE+4;		// banner.

static constexpr auto CHALLENGE_ENTRY_START		=	ID_LOADSAVE+10;		// each of the buttons.
static constexpr auto CHALLENGE_ENTRY_END		=	ID_LOADSAVE+10 +totalslots;  // must have unique ID hmm -Q

static std::shared_ptr<W_SCREEN> psRequestScreen = nullptr; // Widget screen for requester

bool challengesUp = false; ///< True when interface is up and should be run.
bool challengeActive = false; ///< Whether we are running a challenge
std::string challengeName;
WzString challengeFileName;

static void displayLoadBanner(WIDGET const* psWidget, unsigned xOffset, unsigned yOffset)
{
	PIELIGHT col = WZCOL_GREEN;
	auto x = xOffset + psWidget->x();
	auto y = yOffset + psWidget->y();

	pie_BoxFill(x, y, x + psWidget->width(), y + psWidget->height(), col);
	pie_BoxFill(x + 2, y + 2, x + psWidget->width() - 2, y + psWidget->height() - 2, WZCOL_MENU_BACKGROUND);
}

std::string_view currentChallengeName()
{
	if (challengeActive) return challengeName;
	return {};
}

// quite the hack, game name is stored in global sRequestResult
void updateChallenge(bool gameWon)
{
	auto seconds = 0;
  auto newtime = (gameTime - mission.startTime) / GAME_TICKS_PER_SEC;
	auto victory = false;
	WzConfig scores(CHALLENGE_SCORES, WzConfig::ReadAndWrite);
	ASSERT_OR_RETURN(, strlen(sRequestResult) > 0, "Empty sRequestResult");

	auto fStr = strrchr(sRequestResult, '/');
	if (fStr != nullptr) ++fStr; // skip slash
	else fStr = sRequestResult;
	if (*fStr == '\0') {
		debug(LOG_ERROR, "Bad path to challenge file (%s)", sRequestResult);
		return;
	}
	WzString sPath = fStr;
	// remove .json
	if (sPath.endsWith(".json")) {
		sPath.truncate(sPath.length() - 5);
	}
	scores.beginGroup(sPath);
	victory = scores.value("victory", false).toBool();
	seconds = scores.value("seconds", 0).toInt();

	// Update score if we have a victory and best recorded was a loss,
	// or both were losses but time is higher, or both were victories
	// but time is lower.
	if (!victory && gameWon ||
      !gameWon && !victory && newtime > seconds ||
      gameWon && victory && newtime < seconds) {
		scores.setValue("seconds", newtime);
		scores.setValue("victory", gameWon);
		scores.setValue("player", NetPlay.players[selectedPlayer].name);
	}
	scores.endGroup();
}

struct DisplayLoadSlotCache
{
	std::string fullText;
	WzText wzText;
};

struct DisplayLoadSlotData
{
	DisplayLoadSlotCache cache;
	std::string filename;
};

static void displayLoadSlot(WIDGET const* psWidget, unsigned xOffset, unsigned yOffset)
{
	// Any widget using displayLoadSlot must have its pUserData initialized to a (DisplayLoadSlotData*)
	assert(psWidget->pUserData != nullptr);
	auto& data = *static_cast<DisplayLoadSlotData*>(psWidget->pUserData);

	auto x = xOffset + psWidget->x();
	auto y = yOffset + psWidget->y();
	char butString[64];

	drawBlueBox(x, y, psWidget->width(), psWidget->height()); //draw box

  if (((W_BUTTON *) psWidget)->pText.isEmpty()) return;

  sstrcpy(butString, ((W_BUTTON*)psWidget)->pText.toUtf8().c_str());
  if (data.cache.fullText != butString) {
    // Update cache
    while (iV_GetTextWidth(butString, font_regular) > psWidget->width())
    {
      butString[strlen(butString) - 1] = '\0';
    }
    data.cache.wzText.setText(butString, font_regular);
    data.cache.fullText = butString;
  }

  data.cache.wzText.render(x + 4, y + 17, WZCOL_FORM_TEXT);
}

void challengesScreenSizeDidChange(unsigned oldWidth, unsigned oldHeight,
                                   unsigned newWidth, unsigned newHeight)
{
	if (psRequestScreen == nullptr) return;
	psRequestScreen->screenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight);
}

//****************************************************************************************
// Challenge menu
//*****************************************************************************************
bool addChallenges()
{
	char* sPath;
	char* SearchPath = "challenges";
	static std::array<char*, totalslots> sSlotCaps;
	static std::array<char*, totalslots> sSlotTips;
	static std::array<char*, totalslots> sSlotFile;

	psRequestScreen = W_SCREEN::make(); // init the screen

	auto parent = psRequestScreen->psForm.get();

	/* add a form to place the tabbed form on */
	auto challengeForm = std::make_shared<IntFormAnimated>();

	parent->attach(challengeForm);
	challengeForm->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->setGeometry(CHALLENGE_X, CHALLENGE_Y, CHALLENGE_W, (slotsInColumn * CHALLENGE_ENTRY_H +
				CHALLENGE_HGAP * slotsInColumn) + CHALLENGE_BANNER_DEPTH + 20);
			}));

	// Add Banner
	W_FORMINIT sFormInit;
	sFormInit.formID = CHALLENGE_FORM;
	sFormInit.id = CHALLENGE_BANNER;
	sFormInit.style = WFORM_PLAIN;
	sFormInit.x = CHALLENGE_HGAP;
	sFormInit.y = CHALLENGE_VGAP;
	sFormInit.width = CHALLENGE_W - (2 * CHALLENGE_HGAP);
	sFormInit.height = CHALLENGE_BANNER_DEPTH;
	sFormInit.pDisplay = displayLoadBanner;
	sFormInit.UserData = 0;
	widgAddForm(psRequestScreen, &sFormInit);

	// add cancel.
	W_BUTINIT sButInit;
	sButInit.formID = CHALLENGE_BANNER;
	sButInit.x = 8;
	sButInit.y = 8;
	sButInit.width = iV_GetImageWidth(IntImages, IMAGE_NRUTER);
	sButInit.height = iV_GetImageHeight(IntImages, IMAGE_NRUTER);
	sButInit.UserData = PACKDWORD_TRI(0, IMAGE_NRUTER, IMAGE_NRUTER);

	sButInit.id = CHALLENGE_CANCEL;
	sButInit.pTip = _("Close");
	sButInit.pDisplay = intDisplayImageHilight;
	widgAddButton(psRequestScreen, &sButInit);

	// Add Banner Label
	W_LABINIT sLabInit;
	sLabInit.formID = CHALLENGE_BANNER;
	sLabInit.FontID = font_large;
	sLabInit.id = CHALLENGE_LABEL;
	sLabInit.style = WLAB_ALIGNCENTRE;
	sLabInit.x = 0;
	sLabInit.y = 0;
	sLabInit.width = CHALLENGE_W - (2 * CHALLENGE_HGAP); //CHALLENGE_W;
	sLabInit.height = CHALLENGE_BANNER_DEPTH; //This looks right -Q
	sLabInit.pText = WzString::fromUtf8("Challenge");
	widgAddLabel(psRequestScreen, &sLabInit);

	// add slots
	sButInit = W_BUTINIT();
	sButInit.formID = CHALLENGE_FORM;
	sButInit.width = CHALLENGE_ENTRY_W;
	sButInit.height = CHALLENGE_ENTRY_H;
	sButInit.pDisplay = displayLoadSlot;
	sButInit.initPUserDataFunc = []() -> void* { return new DisplayLoadSlotData(); };
	sButInit.onDelete = [](WIDGET* psWidget)
	{
		assert(psWidget->pUserData != nullptr);
		delete static_cast<DisplayLoadSlotData*>(psWidget->pUserData);
		psWidget->pUserData = nullptr;
	};

  auto slotCount = 0;
	for (; slotCount < totalslots; slotCount++)
	{
		sButInit.id = slotCount + CHALLENGE_ENTRY_START;

		if (slotCount < slotsInColumn) {
			sButInit.x = 22 + CHALLENGE_HGAP;
			sButInit.y = ((CHALLENGE_BANNER_DEPTH + (2 * CHALLENGE_VGAP)) + (
				slotCount * (CHALLENGE_VGAP + CHALLENGE_ENTRY_H)));
		}
		else if (slotCount >= slotsInColumn && (slotCount < (slotsInColumn * 2))) {
			sButInit.x = 22 + (2 * CHALLENGE_HGAP + CHALLENGE_ENTRY_W);
			sButInit.y = ((CHALLENGE_BANNER_DEPTH + (2 * CHALLENGE_VGAP)) + (
				(slotCount % slotsInColumn) * (CHALLENGE_VGAP + CHALLENGE_ENTRY_H)));
		}
		else {
			sButInit.x = 22 + (3 * CHALLENGE_HGAP + (2 * CHALLENGE_ENTRY_W));
			sButInit.y = ((CHALLENGE_BANNER_DEPTH + (2 * CHALLENGE_VGAP)) + (
				(slotCount % slotsInColumn) * (CHALLENGE_VGAP + CHALLENGE_ENTRY_H)));
		}
		widgAddButton(psRequestScreen, &sButInit);
	}

	// fill slots.
	slotCount = 0;

	sPath = sSearchPath;
	sPath += "/*.json";

	debug(LOG_SAVE, "Searching \"%s\" for challenges", sPath);

	// add challenges to buttons
	WZ_PHYSFS_enumerateFiles(sSearchPath.c_str(), [&](const char* i) -> bool
	{
		// See if this filename contains the extension we're looking for
		if (!strstr(i, ".json")) {
			// If it doesn't, move on to the next filename
			return true; // continue;
		}

		/* First grab any high score associated with this challenge */
		sPath = i;
    sPath = sPath.substr(sizeof(sPath) - 5, 5); // remove .json
		auto highscore = "no score";
		WzConfig scores(CHALLENGE_SCORES, WzConfig::ReadOnly);
		scores.beginGroup(sPath);
		auto name = scores.value("player", "NO NAME").toWzString();
		auto victory = scores.value("victory", false).toBool();
		auto seconds = scores.value("seconds", -1).toInt();
		if (seconds > 0)
		{
			char psTimeText[sizeof("HH:MM:SS")] = {0};
			struct tm tmp = getUtcTime(static_cast<time_t>(seconds));
			strftime(psTimeText, sizeof(psTimeText), "%H:%M:%S", &tmp);
			highscore = WzString::fromUtf8(psTimeText) + " by " + name + " (" + WzString(victory ? "Victory" : "Survived") + ")";
		}
		scores.endGroup();
		ssprintf(sPath, "%s/%s", sSearchPath, i);
		WzConfig challenge(sPath, WzConfig::ReadOnlyAndRequired);
		ASSERT(challenge.contains("challenge"), "Invalid challenge file %s - no challenge section!", sPath.c_str());
		challenge.beginGroup("challenge");
		ASSERT(challenge.contains("name"), "Invalid challenge file %s - no name", sPath.c_str());
		name = challenge.value("name", "BAD NAME").toWzString();
		ASSERT(challenge.contains("map"), "Invalid challenge file %s - no map", sPath.c_str());
		auto map = challenge.value("map", "BAD MAP").toWzString();
		auto difficulty = challenge.value("difficulty", "BAD DIFFICULTY").toWzString();
		auto description = map + ", " + difficulty + ", " + highscore + ".\n" + challenge.value("description", "").
			toWzString();

		auto button = (W_BUTTON*)widgGetFromID(psRequestScreen, CHALLENGE_ENTRY_START + slotCount);

		debug(LOG_SAVE, "We found [%s]", i);

		/* Set the button-text */
		sstrcpy(sSlotCaps[slotCount], name.toUtf8()); // store it!
		sstrcpy(sSlotTips[slotCount], description.toUtf8()); // store it, too!
		sSlotFile[slotCount] = sPath; // store filename

		/* Add button */
		button->pTip = sSlotTips[slotCount];
		button->pText = WzString::fromUtf8(sSlotCaps[slotCount]);
		assert(button->pUserData != nullptr);
		static_cast<DisplayLoadSlotData*>(button->pUserData)->filename = sSlotFile[slotCount];
		slotCount++; // go to next button...
		if (slotCount == totalslots) {
			return false; // break;
		}
		challenge.endGroup();
		return true; // continue
	});

	challengesUp = true;

	return true;
}

// ////////////////////////////////////////////////////////////////////////////
bool closeChallenges()
{
	psRequestScreen = nullptr;
	// need to "eat" up the return key so it don't pass back to game.
	inputLoseFocus();
	challengesUp = false;
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// Returns true if cancel pressed or a valid game slot was selected.
// if when returning true strlen(sRequestResult) != 0 then a valid game
// slot was selected otherwise cancel was selected..
bool runChallenges()
{
	WidgetTriggers const& triggers = widgRunScreen(psRequestScreen);
	for (const auto& trigger : triggers)
	{
		unsigned id = trigger.widget->id;

		sstrcpy(sRequestResult, ""); // set returned filename to null;

		// cancel this operation...
		if (id == CHALLENGE_CANCEL || CancelPressed())
		{
			goto failure;
		}

		// clicked a load entry
		if (id >= CHALLENGE_ENTRY_START && id <= CHALLENGE_ENTRY_END) {
			auto psWidget = dynamic_cast<W_BUTTON*>(widgGetFromID(psRequestScreen, id));
			assert(psWidget != nullptr);
			if (!(psWidget->pText.isEmpty()))
			{
				auto data = static_cast<DisplayLoadSlotData*>(psWidget->pUserData);
				assert(data != nullptr);
				assert(data->filename != nullptr);
				sstrcpy(sRequestResult, data->filename);
				challengeFileName = sRequestResult;
				challengeName = psWidget->pText.toStdString();
			}
			else
			{
				goto failure; // clicked on an empty box
			}
			goto success;
		}
	}

	return false;

	// failed and/or cancelled..
failure:
	closeChallenges();
	challengeActive = false;
	return false;

	// success on load.
success:
	closeChallenges();
	challengeActive = true;
	ingame.side = InGameSide::HOST_OR_SINGLEPLAYER;
	changeTitleUI(std::make_shared<WzMultiplayerOptionsTitleUI>(wzTitleUICurrent));
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// should be done when drawing the other widgets.
bool displayChallenges()
{
	widgDisplayScreen(psRequestScreen); // display widgets.
	return true;
}
