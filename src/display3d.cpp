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
 * @file display3d.cpp
 * Draws the 3D view
 * 
 * Originally by Alex McLean & Jeremy Sallis, Pumpkin Studios, EIDOS INTERACTIVE
 */

#include "lib/framework/math_ext.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/piefunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/piemode.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/netplay/netplay.h"
#include "lib/sound/audio.h"
#include "lib/widget/widget.h"

#include "animation.h"
#include "atmos.h"
#include "baseobject.h"
#include "bucket3d.h"
#include "component.h"
#include "display.h"
#include "display3d.h"
#include "displaydef.h"
#include "edit3d.h"
#include "effects.h"
#include "faction.h"
#include "geometry.h"
#include "intimage.h"
#include "levels.h"
#include "lighting.h"
#include "loop.h"
#include "miscimd.h"
#include "move.h"
#include "multiplay.h"
#include "multistat.h"
#include "projectile.h"
#include "radar.h"
#include "scores.h"
#include "terrain.h"
#include "warcam.h"
#include "warzoneconfig.h"
#include "cmddroid.h"


static void displayDelivPoints(const glm::mat4& viewMatrix);
static void displayProximityMsgs(const glm::mat4& viewMatrix);
static void displayDynamicObjects(const glm::mat4& viewMatrix);
static void displayStaticObjects(const glm::mat4& viewMatrix);
static void displayFeatures(const glm::mat4& viewMatrix);
static UDWORD getTargettingGfx();
static void drawDroidGroupNumber(Droid* psDroid);
static void trackHeight(int desiredHeight);
static void renderSurroundings(const glm::mat4& viewMatrix);
static void locateMouse();
static bool renderWallSection(Structure* psStructure, const glm::mat4& viewMatrix);
static void drawDragBox();
static void calcFlagPosScreenCoords(SDWORD* pX, SDWORD* pY, SDWORD* pR, const glm::mat4& modelViewMatrix);
static void drawTiles(iView* player);
static void display3DProjectiles(const glm::mat4& viewMatrix);
static void drawDroidSelections();
static void drawStructureSelections();
static void displayBlueprints(const glm::mat4& viewMatrix);
static void processSensorTarget();
static void processDestinationTarget();
static bool eitherSelected(Droid* psDroid);
static void structureEffects();
static void showDroidSensorRanges();
static void showSensorRange2(BaseObject * psObj);
static void drawRangeAtPos(SDWORD centerX, SDWORD centerY, SDWORD radius);
static void addConstructionLine(Droid* psDroid, Structure* psStructure, const glm::mat4& viewMatrix);
static void doConstructionLines(const glm::mat4& viewMatrix);
static void drawDroidCmndNo(Droid* psDroid);
static void drawDroidOrder(const Droid* psDroid);
static void drawDroidRank(Droid* psDroid);
static void drawDroidSensorLock(Droid* psDroid);
static int calcAverageTerrainHeight(int tileX, int tileZ);
static int calculateCameraHeight(int height);
static void updatePlayerAverageCentreTerrainHeight();

static PIELIGHT getBlueprintColour(STRUCTURE_STATE state);

static void NetworkDisplayImage(WIDGET* psWidget, unsigned xOffset, unsigned yOffset);
extern bool writeGameInfo(const char* pFileName);
// Used to help debug issues when we have fatal errors & crash handler testing.

static WzText txtLevelName;
static WzText txtDebugStatus;
static WzText txtCurrentTime;
static WzText txtShowFPS;
static WzText txtUnits;
// show Samples text
static WzText txtShowSamples_Que;
static WzText txtShowSamples_Lst;
static WzText txtShowSamples_Act;
// show Orders text
static WzText txtShowOrders;
// show Droid visible/draw counts text
static WzText droidText;


// Should be cleaned up properly and be put in structures.

// Initialised at start of drawTiles().
// In model coordinates where x is east, y is up and z is north, rather than world coordinates where x is east, y is south and z is up.
// To get the real camera position, still need to add Vector3i(player.p.x, 0, player.p.z).
static Vector3i actualCameraPosition;

bool bRender3DOnly;
static bool bRangeDisplay = false;
static int rangeCenterX, rangeCenterY, rangeRadius;
static bool bDrawProximitys = true;
bool showGateways = false;
bool showPath = false;

// Skybox data
static float wind = 0.0f;
static float windSpeed = 0.0f;
static float skybox_scale = 10000.0f;

/// When to display HP bars
UWORD barMode;

/// Have we made a selection by clicking the mouse? - used for dragging etc
bool selectAttempt = false;

/// Vectors that hold the player and camera directions and positions
iView playerPos;

/// How far away are we from the terrain
static float distance;

/// Stores the screen coordinates of the transformed terrain tiles
static Vector3i tileScreenInfo[VISIBLE_YTILES + 1][VISIBLE_XTILES + 1];
static bool tileScreenVisible[VISIBLE_YTILES + 1][VISIBLE_XTILES + 1] = {false};

/// Records the present X and Y values for the current mouse tile (in tiles)
int mouseTileX, mouseTileY;
Vector2i mousePos(0, 0);

/// Do we want the radar to be rendered
bool radarOnScreen = true;
bool radarPermitted = true;

bool radarVisible()
{
	if (radarOnScreen && radarPermitted && getWidgetsStatus()) {
		return true;
	}
	else {
		return false;
	}
}

/// Show unit/building gun/sensor range
bool rangeOnScreen = false; // For now, most likely will change later!  -Q 5-10-05   A very nice effect - Per

/// Tactical UI: show/hide target origin icon
bool tuiTargetOrigin = false;

/// Temporary values for the terrain render - center of grid to be rendered
static unsigned playerXTile, playerZTile;

/// The cached value of frameGetFrameNumber()
static UDWORD currentGameFrame;
/// The box used for multiple selection - present screen coordinates
static QUAD dragQuad;

/** Number of tiles visible
 * \todo This should become dynamic! (A function of resolution, angle and zoom maybe.)
 */
const Vector2i visibleTiles(VISIBLE_XTILES, VISIBLE_YTILES);

/// The tile we use for drawing the bottom of a body of water
static unsigned int underwaterTile = WATER_TILE;
/** The tile we use for drawing rubble
 * \note Unused.
 */
static unsigned int rubbleTile = BLOCKING_RUBBLE_TILE;

/** Show how many frames we are rendering per second
 * default OFF, turn ON via console command 'showfps'
 */
bool showFPS = false; //
/** Show how many samples we are rendering per second
 * default OFF, turn ON via console command 'showsamples'
 */
bool showUNITCOUNT = false;
/** Show how many kills/deaths (produced units) made
 * default OFF, turn ON via console command 'showunits'
 */
bool showSAMPLES = false;
/**  Show the current selected units order / action
 *  default OFF, turn ON via console command 'showorders'
 */
bool showORDERS = false;
/**  Show the drawn/undrawn counts for droids
  * default OFF, turn ON by flipping it here
  */
bool showDROIDcounts = false;

/**  Speed of blueprints animation (moving from one tile to another)
  * default 20, change in config
  */
int BlueprintTrackAnimationSpeed = 20;

/** When we have a connection issue, we will flash a message on screen
*/
static const char* errorWaiting = nullptr;
static uint32_t lastErrorTime = 0;

#define NETWORK_FORM_ID 0xFAAA
#define NETWORK_BUT_ID 0xFAAB
/** When enabled, this causes a segfault in the game, to test out the crash handler */
bool CauseCrash = false;

/** tells us in realtime, what droid is doing (order / action)
*/
char DROIDDOING[512];

/// Geometric offset which will be passed to pie_SetGeometricOffset
static const int geoOffset = 192;

/// The average terrain height for the center of the area the camera is looking at
static int averageCentreTerrainHeight;

/** The time at which a sensor target was last assigned
 * Used to draw a visual effect.
 */
static UDWORD lastTargetAssignation = 0;
/** The time at which an order concerning a destination was last given
 * Used to draw a visual effect.
 */
static UDWORD lastDestAssignation = 0;
static bool bSensorTargetting = false;
static bool bDestTargetting = false;
static BaseObject * psSensorObj = nullptr;
static UDWORD destTargetX, destTargetY;
static UDWORD destTileX = 0, destTileY = 0;

struct Blueprint
{
	Blueprint()
		: stats()
		  , pos({0, 0, 0})
		  , dir(0)
		  , index(0)
		  , state(STRUCTURE_STATE::BLUEPRINT_INVALID)
		  , player(selectedPlayer)
	{
	}

	Blueprint(StructureStats const* stats, Vector3i pos, uint16_t dir, 
            unsigned index, STRUCTURE_STATE state, unsigned player)
		: stats()
		  , pos(pos)
		  , dir(dir)
		  , index(index)
		  , state(state)
		  , player(player)
	{
	}

	[[nodiscard]] int compare(Blueprint const& b) const
	{
		if (stats.ref != b.stats.ref) {
			return stats.ref < b.stats.ref ? -1 : 1;
		}
		if (pos.x != b.pos.x) {
			return pos.x < b.pos.x ? -1 : 1;
		}
		if (pos.y != b.pos.y) {
			return pos.y < b.pos.y ? -1 : 1;
		}
		if (pos.z != b.pos.z) {
			return pos.z < b.pos.z ? -1 : 1;
		}
		if (dir != b.dir) {
			return dir < b.dir ? -1 : 1;
		}
		if (index != b.index) {
			return index < b.index ? -1 : 1;
		}
		if (state != b.state) {
			return state < b.state ? -1 : 1;
		}
		return 0;
	}

	bool operator <(Blueprint const& b) const
	{
		return compare(b) < 0;
	}

	bool operator ==(Blueprint const& b) const
	{
		return compare(b) == 0;
	}

	[[nodiscard]] Structure* buildBlueprint() const ///< Must delete after use.
	{
		return ::buildBlueprint(&stats, pos, dir, index, state, player);
	}

	void renderBlueprint(const glm::mat4& viewMatrix) const
	{
		if (clipXY(pos.x, pos.y)) {
			Structure* psStruct = buildBlueprint();
			ASSERT_OR_RETURN(, psStruct != nullptr, "buildBlueprint returned nullptr");
			renderStructure(psStruct, viewMatrix);
			delete psStruct;
		}
	}

	StructureStats stats;
	Vector3i pos;
	uint16_t dir;
	unsigned index;
	STRUCTURE_STATE state;
	uint8_t player;
};

static std::vector<Blueprint> blueprints;

#define	TARGET_TO_SENSOR_TIME	((4*(GAME_TICKS_PER_SEC))/5)
#define	DEST_TARGET_TIME	(GAME_TICKS_PER_SEC/4)

/// The distance the selection box will pulse
static const float BOX_PULSE_SIZE = 30;

/// the opacity at which building blueprints will be drawn
static const int BLUEPRINT_OPACITY = 120;

/********************  Functions  ********************/

void display3dScreenSizeDidChange(unsigned oldWidth, unsigned oldHeight, 
                                  unsigned newWidth, unsigned newHeight)
{
	resizeRadar(); // recalculate radar position
}

float interpolateAngleDegrees(int a, int b, float t)
{
	if (a > 180) {
		a -= 360;
	}
	if (b > 180) {
		b -= 360;
	}
	float d = b - a;
	return a + d * t;
}

bool drawShape(BaseObject const* psObj, iIMDShape* strImd, int colour, PIELIGHT buildingBrightness, int pieFlag,
               int pieFlagData, const glm::mat4& viewMatrix)
{
	glm::mat4 modelMatrix(1.f);
	auto animFrame = 0; // for texture animation
	if (strImd->numFrames > 0) // Calculate an animation frame
	{
		animFrame = getModularScaledGraphicsTime(strImd->animInterval, strImd->numFrames);
	}
	if (strImd->objanimframes) {
		auto elapsed = graphicsTime - psObj->timeAnimationStarted;
		if (elapsed < 0)
		{
			elapsed = 0; // Animation hasn't started yet.
		}

		const int frame = (elapsed / strImd->objanimtime) % strImd->objanimframes;
		ASSERT(frame < strImd->objanimframes, "Bad index %d >= %d", frame, strImd->objanimframes);

		const auto& state = strImd->objanimdata.at(frame);

		if (state.scale.x == -1.0f) // disabled frame, for implementing key frame animation
		{
			return false;
		}

		if (strImd->interpolate == 1) {
			const auto frameFraction = static_cast<float>(fmod(elapsed / (float)strImd->objanimtime,
			                                                    strImd->objanimframes) - frame);
			const auto nextFrame = (frame + 1) % strImd->objanimframes;
			const ANIMFRAME& nextState = strImd->objanimdata.at(nextFrame);

			modelMatrix *=
				glm::interpolate(glm::translate(glm::vec3(state.pos)), glm::translate(glm::vec3(nextState.pos)),
				                 frameFraction) *
				glm::rotate(
					RADIANS(interpolateAngleDegrees(state.rot.pitch / DEG(1), nextState.rot.pitch / DEG(1),
					                                frameFraction)), glm::vec3(1.f, 0.f, 0.f)) *
				glm::rotate(
					RADIANS(interpolateAngleDegrees(state.rot.direction / DEG(1), nextState.rot.direction / DEG(1),
					                                frameFraction)), glm::vec3(0.f, 1.f, 0.f)) *
				glm::rotate(
					RADIANS(
						interpolateAngleDegrees(state.rot.roll / DEG(1), nextState.rot.roll / DEG(1), frameFraction)),
					glm::vec3(0.f, 0.f, 1.f)) *
				glm::scale(state.scale);
		}
		else
		{
			modelMatrix *= glm::translate(glm::vec3(state.pos)) *
				glm::rotate(UNDEG(state.rot.pitch), glm::vec3(1.f, 0.f, 0.f)) *
				glm::rotate(UNDEG(state.rot.direction), glm::vec3(0.f, 1.f, 0.f)) *
				glm::rotate(UNDEG(state.rot.roll), glm::vec3(0.f, 0.f, 1.f)) *
				glm::scale(state.scale);
		}
	}

	return pie_Draw3DShape(strImd, animFrame, colour, buildingBrightness, pieFlag, pieFlagData,
	                       viewMatrix * modelMatrix);
}

static void setScreenDisp(DisplayData* sDisplay, const glm::mat4& modelViewMatrix)
{
	Vector3i zero(0, 0, 0);
	Vector2i s(0, 0);

	pie_RotateProject(&zero, modelViewMatrix, &s);
	sDisplay->screen_x = s.x;
	sDisplay->screen_y = s.y;
}

void setSkyBox(const char* page, float mywind, float myscale)
{
	windSpeed = mywind;
	wind = 0.0f;
	skybox_scale = myscale;
	pie_Skybox_Texture(page);
}

static inline void rotateSomething(int& x, int& y, uint16_t angle)
{
	auto cra = iCos(angle), sra = iSin(angle);
	auto newX = (x * cra - y * sra) >> 16;
	auto newY = (x * sra + y * cra) >> 16;
	x = newX;
	y = newY;
}

static Blueprint getTileBlueprint(int mapX, int mapY)
{
	Vector2i mouse(world_coord(mapX) + TILE_UNITS / 2, world_coord(mapY) + TILE_UNITS / 2);

	for (auto const& blueprint : blueprints)
	{
		const auto size = blueprint.stats.size(blueprint.dir) * TILE_UNITS;
		if (abs(mouse.x - blueprint.pos.x) < size.x / 2 && abs(mouse.y - blueprint.pos.y) < size.y / 2) {
			return blueprint;
		}
	}
	return {nullptr, Vector3i(), 0, 0, STRUCTURE_STATE::BEING_BUILT, selectedPlayer};
}

Structure* getTileBlueprintStructure(int mapX, int mapY)
{
	static Structure* psStruct = nullptr;

	Blueprint blueprint = getTileBlueprint(mapX, mapY);
	if (blueprint.state == STRUCTURE_STATE::BLUEPRINT_PLANNED) {
		delete psStruct; // Delete previously returned structure, if any.
		psStruct = blueprint.buildBlueprint();
		return psStruct; // This blueprint was clicked on.
	}
	return nullptr;
}

StructureStats getTileBlueprintStats(int mapX, int mapY)
{
	return getTileBlueprint(mapX, mapY).stats;
}

bool anyBlueprintTooClose(StructureStats const* stats, Vector2i pos, uint16_t dir)
{
  using enum STRUCTURE_STATE;
	for (auto& blueprint : blueprints)
	{
		if ((blueprint.state == BLUEPRINT_PLANNED || 
         blueprint.state == BLUEPRINT_PLANNED_BY_ALLY &&
        ::isBlueprintTooClose(stats, pos, dir, &blueprint.stats, blueprint.pos, blueprint.dir))) {
			return true;
		}
	}
	return false;
}

void clearBlueprints()
{
	blueprints.clear();
}

static PIELIGHT selectionBrightness()
{
	unsigned brightVar;
	if (!gamePaused()) {
		brightVar = getModularScaledGraphicsTime(990, 110);
		if (brightVar > 55) {
			brightVar = 110 - brightVar;
		}
	}
	else {
		brightVar = 55;
	}
	return pal_SetBrightness(200 + brightVar);
}

static PIELIGHT structureBrightness(Structure* psStructure)
{
	PIELIGHT buildingBrightness;

	if (structureIsBlueprint(psStructure)) {
		buildingBrightness = getBlueprintColour(psStructure->getState());
	}
	else {
		buildingBrightness = pal_SetBrightness(
			static_cast<UBYTE>(200 - 100 / 65536.f * getStructureDamage(psStructure)));

		/* If it's selected, then it's brighter */
		if (psStructure->damageManager->isSelected()) {
			buildingBrightness = selectionBrightness();
		}
		if (!getRevealStatus()) {
			buildingBrightness = pal_SetBrightness(
				avGetObjLightLevel((BaseObject *)psStructure, buildingBrightness.byte.r));
		}
		if (!hasSensorOnTile(mapTile(
            map_coord(psStructure->getPosition().x), 
            map_coord(psStructure->getPosition().y)), 
                         selectedPlayer)) {
			buildingBrightness.byte.r /= 2;
			buildingBrightness.byte.g /= 2;
			buildingBrightness.byte.b /= 2;
		}
	}
	return buildingBrightness;
}


/// Show all droid movement parts by displaying an explosion at every step
static void showDroidPaths()
{
	if ((graphicsTime / 250 % 2) != 0) {
		return;
	}

	if (selectedPlayer >= MAX_PLAYERS) {
		return; // no-op for now
	}

	for (auto& psDroid : playerList[selectedPlayer].droids)
	{
		if (psDroid.damageManager->isSelected() && 
        psDroid.getMovementData()->status != MOVE_STATUS::INACTIVE) {
			const auto len = psDroid.getMovementData()->path.size();
			for (int i = std::max(psDroid.getMovementData()->pathIndex - 1, 0); i < len; i++)
			{
				Vector3i pos;

				ASSERT(worldOnMap(psDroid.getMovementData()->path[i].x, 
                          psDroid.getMovementData()->path[i].y), 
               "Path off map!");
        
				pos.x = psDroid.getMovementData()->path[i].x;
				pos.z = psDroid.getMovementData()->path[i].y;
				pos.y = map_Height(pos.x, pos.z) + 16;

				effectGiveAuxVar(80);
				addEffect(&pos, EFFECT_GROUP::EXPLOSION, 
                  EFFECT_TYPE::EXPLOSION_TYPE_LASER, 
                  false, nullptr, 0);
			}
		}
	}
}

/// Displays an image for the Network Issue button
static void NetworkDisplayImage(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset)
{
	int x = xOffset + psWidget->x();
	int y = yOffset + psWidget->y();
	UWORD ImageID;
	auto status = (CONNECTION_STATUS)UNPACKDWORD_TRI_A(psWidget->UserData);

	ASSERT(psWidget->type == WIDG_BUTTON, "Not a button");

	// cheap way to do a button flash
	if ((realTime / 250) % 2 == 0)
	{
		ImageID = UNPACKDWORD_TRI_B(psWidget->UserData);
	}
	else
	{
		ImageID = UNPACKDWORD_TRI_C(psWidget->UserData);
	}

	if (NETcheckPlayerConnectionStatus(status, NET_ALL_PLAYERS))
	{
		unsigned c = 0;
		char players[MAX_PLAYERS + 1];
		PlayerMask playerMaskMapped = 0;
		for (unsigned n = 0; n < MAX_PLAYERS; ++n)
		{
			if (NETcheckPlayerConnectionStatus(status, n))
			{
				playerMaskMapped |= 1 << NetPlay.players[n].position;
			}
		}
		for (unsigned n = 0; n < MAX_PLAYERS; ++n)
		{
			if ((playerMaskMapped & 1 << n) != 0)
			{
				STATIC_ASSERT(MAX_PLAYERS <= 32);
				// If increasing MAX_PLAYERS, check all the 1<<playerNumber shifts, since the 1 is usually a 32-bit type.
				players[c++] = "0123456789ABCDEFGHIJKLMNOPQRSTUV"[n];
			}
		}
		players[c] = '\0';
		const unsigned width = iV_GetTextWidth(players, font_regular) + 10;
		const unsigned height = iV_GetTextHeight(players, font_regular) + 10;
		iV_SetTextColour(WZCOL_TEXT_BRIGHT);
		iV_DrawText(players, x - width, y + height, font_regular);
	}

	iV_DrawImage(IntImages, ImageID, x, y);
}

static void setupConnectionStatusForm()
{
	static unsigned prevStatusMask = 0;

	const int separation = 3;
	unsigned statusMask = 0;
	unsigned total = 0;

	for (unsigned i = 0; i < CONNECTIONSTATUS_NORMAL; ++i)
	{
		if (NETcheckPlayerConnectionStatus((CONNECTION_STATUS)i, NET_ALL_PLAYERS))
		{
			statusMask |= 1 << i;
			++total;
		}
	}

	if (prevStatusMask != 0 && statusMask != prevStatusMask)
	{
		// Remove the icons.
		for (unsigned i = 0; i < CONNECTIONSTATUS_NORMAL; ++i)
		{
			if ((statusMask & 1 << i) != 0)
			{
				widgDelete(psWScreen, NETWORK_BUT_ID + i); // kill button
			}
		}
		widgDelete(psWScreen, NETWORK_FORM_ID); // kill form

		prevStatusMask = 0;
	}

	if (prevStatusMask == 0 && statusMask != 0)
	{
		unsigned n = 0;
		// Create the basic form
		W_FORMINIT sFormInit;
		sFormInit.formID = 0;
		sFormInit.id = NETWORK_FORM_ID;
		sFormInit.style = WFORM_PLAIN;
		sFormInit.calcLayout = LAMBDA_CALCLAYOUT_SIMPLE({
				psWidget->move((int)(pie_GetVideoBufferWidth() - 52), 80);
				});
		sFormInit.width = 36;
		sFormInit.height = (24 + separation) * total - separation;
		if (!widgAddForm(psWScreen, &sFormInit))
		{
			//return false;
		}

		/* Now add the buttons */
		for (unsigned i = 0; i < CONNECTIONSTATUS_NORMAL; ++i)
		{
			if ((statusMask & 1 << i) == 0)
			{
				continue;
			}

			//set up default button data
			W_BUTINIT sButInit;
			sButInit.formID = NETWORK_FORM_ID;
			sButInit.id = NETWORK_BUT_ID + i;
			sButInit.width = 36;
			sButInit.height = 24;

			//add button
			sButInit.style = WBUT_PLAIN;
			sButInit.x = 0;
			sButInit.y = (24 + separation) * n;
			sButInit.pDisplay = NetworkDisplayImage;
			// Note we would set the image to be different based on which issue it is.
			switch (i)
			{
			default:
				ASSERT(false, "Bad connection status value.");
				sButInit.pTip = "Bug";
				sButInit.UserData = PACKDWORD_TRI(0, IMAGE_DESYNC_HI, IMAGE_PLAYER_LEFT_LO);
				break;
			case CONNECTIONSTATUS_PLAYER_LEAVING:
				sButInit.pTip = _("Player left");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_PLAYER_LEFT_HI, IMAGE_PLAYER_LEFT_LO);
				break;
			case CONNECTIONSTATUS_PLAYER_DROPPED:
				sButInit.pTip = _("Player dropped");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_DISCONNECT_LO, IMAGE_DISCONNECT_HI);
				break;
			case CONNECTIONSTATUS_WAITING_FOR_PLAYER:
				sButInit.pTip = _("Waiting for other players");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_WAITING_HI, IMAGE_WAITING_LO);

				break;
			case CONNECTIONSTATUS_DESYNC:
				sButInit.pTip = _("Out of sync");
				sButInit.UserData = PACKDWORD_TRI(i, IMAGE_DESYNC_HI, IMAGE_DESYNC_LO);
				break;
			}

			if (!widgAddButton(psWScreen, &sButInit))
			{
				//return false;
			}

			++n;
		}

		prevStatusMask = statusMask;
	}
}

/// Render the 3D world
void draw3DScene()
{
	wzPerfBegin(PERF_START_FRAME, "Start 3D scene");

	/* What frame number are we on? */
	currentGameFrame = frameGetFrameNumber();

	// Tell shader system what the time is
	pie_SetShaderTime(graphicsTime);

	/* Build the drag quad */
	if (dragBox3D.status == DRAG_RELEASED)
	{
		dragQuad.coords[0].x = dragBox3D.x1; // TOP LEFT
		dragQuad.coords[0].y = dragBox3D.y1;

		dragQuad.coords[1].x = dragBox3D.x2; // TOP RIGHT
		dragQuad.coords[1].y = dragBox3D.y1;

		dragQuad.coords[2].x = dragBox3D.x2; // BOTTOM RIGHT
		dragQuad.coords[2].y = dragBox3D.y2;

		dragQuad.coords[3].x = dragBox3D.x1; // BOTTOM LEFT
		dragQuad.coords[3].y = dragBox3D.y2;
	}

	pie_Begin3DScene();
	/* Set 3D world origins */
	pie_SetGeometricOffset(rendSurface.width / 2, geoOffset);

	updateFogDistance(distance);

	/* Now, draw the terrain */
	drawTiles(&playerPos);

	wzPerfBegin(PERF_MISC, "3D scene - misc and text");

	/* Show the drag Box if necessary */
	drawDragBox();

	/* Have we released the drag box? */
	if (dragBox3D.status == DRAG_RELEASED)
	{
		dragBox3D.status = DRAG_INACTIVE;
	}

	pie_BeginInterface();
	drawDroidSelections();

	drawStructureSelections();

	if (!bRender3DOnly)
	{
		if (radarVisible())
		{
			pie_SetFogStatus(false);
			gfx_api::context::get().debugStringMarker("Draw 3D scene - radar");
			drawRadar();
			pie_SetFogStatus(true);
		}

		/* Ensure that any text messages are displayed at bottom of screen */
		pie_SetFogStatus(false);
		displayConsoleMessages();
		bRender3DOnly = true;
	}

	pie_SetFogStatus(false);
	iV_SetTextColour(WZCOL_TEXT_BRIGHT);

	/* Dont remove this folks!!!! */
	if (errorWaiting)
	{
		// print the error message if none have been printed for one minute
		if (lastErrorTime == 0 || lastErrorTime + (60 * GAME_TICKS_PER_SEC) < realTime)
		{
			char trimMsg[255];
			audio_PlayBuildFailedOnce();
			ssprintf(trimMsg, "Error! (Check your logs!): %.78s", errorWaiting);
			addConsoleMessage(trimMsg, CONSOLE_TEXT_JUSTIFICATION::DEFAULT, NOTIFY_MESSAGE);
			errorWaiting = nullptr;
			lastErrorTime = realTime;
		}
	}
	else
	{
		errorWaiting = debugLastError();
	}
	if (showSAMPLES) //Displays the number of sound samples we currently have
	{
		unsigned int width, height;
		std::string Qbuf, Lbuf, Abuf;

		Qbuf = astringf("Que: %04u", audio_GetSampleQueueCount());
		Lbuf = astringf("Lst: %04u", audio_GetSampleListCount());
		Abuf = astringf("Act: %04u", sound_GetActiveSamplesCount());
		txtShowSamples_Que.setText(Qbuf, font_regular);
		txtShowSamples_Lst.setText(Lbuf, font_regular);
		txtShowSamples_Act.setText(Abuf, font_regular);

		width = txtShowSamples_Que.width() + 11;
		height = txtShowSamples_Que.height();

		txtShowSamples_Que.render(pie_GetVideoBufferWidth() - width, height + 2, WZCOL_TEXT_BRIGHT);
		txtShowSamples_Lst.render(pie_GetVideoBufferWidth() - width, height + 48, WZCOL_TEXT_BRIGHT);
		txtShowSamples_Act.render(pie_GetVideoBufferWidth() - width, height + 59, WZCOL_TEXT_BRIGHT);
	}
	if (showFPS)
	{
		std::string fps = astringf("FPS: %d", frameRate());
		txtShowFPS.setText(fps, font_regular);
		const unsigned width = txtShowFPS.width() + 10;
		const unsigned height = 9; //txtShowFPS.height();
		txtShowFPS.render(pie_GetVideoBufferWidth() - width, pie_GetVideoBufferHeight() - height, WZCOL_TEXT_BRIGHT);
	}
	if (showUNITCOUNT && selectedPlayer < MAX_PLAYERS)
	{
		std::string killdiff = astringf("Units: %u lost / %u built / %u killed", missionData.unitsLost,
		                                missionData.unitsBuilt, getSelectedPlayerUnitsKilled());
		txtUnits.setText(killdiff, font_regular);
		const unsigned width = txtUnits.width() + 10;
		const unsigned height = 9; //txtUnits.height();
		txtUnits.render(pie_GetVideoBufferWidth() - width - ((showFPS) ? txtShowFPS.width() + 10 : 0),
		                pie_GetVideoBufferHeight() - height, WZCOL_TEXT_BRIGHT);
	}
	if (showORDERS)
	{
		unsigned int height;
		txtShowOrders.setText(DROIDDOING, font_regular);
		height = txtShowOrders.height();
		txtShowOrders.render(0, pie_GetVideoBufferHeight() - height, WZCOL_TEXT_BRIGHT);
	}
	if (showDROIDcounts && selectedPlayer < MAX_PLAYERS)
	{
		int visibleDroids = 0;
		int undrawnDroids = 0;
		for (auto& psDroid : playerList[selectedPlayer].droids)
		{
			if (psDroid.getDisplayData()->frame_number != currentGameFrame) {
				++undrawnDroids;
				continue;
			}
			++visibleDroids;
		}
		char droidCounts[255];
		sprintf(droidCounts, "Droids: %d drawn, %d undrawn", visibleDroids, undrawnDroids);
		droidText.setText(droidCounts, font_regular);
		droidText.render(pie_GetVideoBufferWidth() - droidText.width() - 10, droidText.height() + 2, WZCOL_TEXT_BRIGHT);
	}

	setupConnectionStatusForm();

	if (getWidgetsStatus() && !gamePaused()) {
		char buildInfo[255];
		getAsciiTime(buildInfo, graphicsTime);
		txtLevelName.render(RET_X + 134, 410 + E_H, WZCOL_TEXT_MEDIUM);
		const DebugInputManager& dbgInputManager = gInputManager.debugManager();
		if (dbgInputManager.debugMappingsAllowed()) {
			txtDebugStatus.render(RET_X + 134, 436 + E_H, WZCOL_TEXT_MEDIUM);
		}
		txtCurrentTime.setText(buildInfo, font_small);
		txtCurrentTime.render(RET_X + 134, 422 + E_H, WZCOL_TEXT_MEDIUM);
	}

	while (playerPos.r.y > DEG(360))
	{
		playerPos.r.y -= DEG(360);
	}

	/* If we don't have an active camera track, then track terrain height! */
	if (!getWarCamStatus())
	{
		/* Move the autonomous camera if necessary */
		updatePlayerAverageCentreTerrainHeight();
		trackHeight(calculateCameraHeight(averageCentreTerrainHeight));
	}
	else
	{
		processWarCam();
	}

	processSensorTarget();
	processDestinationTarget();

	structureEffects(); // add fancy effects to structures

	showDroidSensorRanges(); //shows sensor data for units/droids/whatever...-Q 5-10-05
	if (CauseCrash)
	{
		char* crash = nullptr;
#ifdef DEBUG
		ASSERT(false,
		       "Yes, this is a assert.  This should not happen on release builds! Use --noassert to bypass in debug builds.");
		debug(LOG_WARNING, " *** Warning!  You have compiled in debug mode! ***");
#endif
		writeGameInfo("WZdebuginfo.txt"); //also test writing out this file.
		debug(LOG_FATAL, "Forcing a segfault! (crash handler test)");
		// and here comes the crash
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
		*crash = 0x3; // deliberate null-dereference
#if defined(WZ_CC_GNU) && !defined(WZ_CC_INTEL) && !defined(WZ_CC_CLANG) && (7 <= __GNUC__)
# pragma GCC diagnostic pop
#endif
		exit(-1); // will never reach this, but just in case...
	}
	//visualize radius if needed
	if (bRangeDisplay)
	{
		drawRangeAtPos(rangeCenterX, rangeCenterY, rangeRadius);
	}

	if (showPath)
	{
		showDroidPaths();
	}

	wzPerfEnd(PERF_MISC);
}


/***************************************************************************/
bool doWeDrawProximitys()
{
	return (bDrawProximitys);
}

/***************************************************************************/
void setProximityDraw(bool val)
{
	bDrawProximitys = val;
}

/***************************************************************************/
/// Calculate the average terrain height for the area directly below the tile
static int calcAverageTerrainHeight(int tileX, int tileZ)
{
	int numTilesAveraged = 0;

	/**
	 * We track the height here - so make sure we get the average heights
	 * of the tiles directly underneath us
	 */
	int result = 0;
	for (int i = -4; i <= 4; i++)
	{
		for (int j = -4; j <= 4; j++)
		{
			if (tileOnMap(tileX + j, tileZ + i))
			{
				/* Get a pointer to the tile at this location */
				Tile* psTile = mapTile(tileX + j, tileZ + i);

				result += psTile->height;
				numTilesAveraged++;
			}
		}
	}
	if (numTilesAveraged == 0) // might be if off map
	{
		return ELEVATION_SCALE * TILE_UNITS;
	}

	/**
	 * Work out the average height.
	 * We use this information to keep the player camera above the terrain.
	 */
	Tile* psTile = mapTile(tileX, tileZ);

	result /= numTilesAveraged;
	if (result < psTile->height)
	{
		result = psTile->height;
	}

	return result;
}

static void updatePlayerAverageCentreTerrainHeight()
{
	averageCentreTerrainHeight = calcAverageTerrainHeight(playerXTile, playerZTile);
}

inline bool quadIntersectsWithScreen(const QUAD& quad)
{
	const auto width = pie_GetVideoBufferWidth();
	const auto height = pie_GetVideoBufferHeight();
	for (auto coord : quad.coords)
	{
		if ((coord.x < 0) || (coord.x > width)) {
			continue;
		}
		if ((coord.y < 0) || (coord.y > height)) {
			continue;
		}
		return true; // corner (x,y) is within the screen bounds
	}
	return false;
}

/// Draw the terrain and all droids, missiles and other objects on it
static void drawTiles(iView* player)
{
	// draw terrain

	/* Do boundary and extent checking                                  */

	/* Find our position in tile coordinates */
	playerXTile = map_coord(player->p.x);
	playerZTile = map_coord(player->p.z);

	/* Set up the geometry                                              */
  
	const glm::mat4& viewMatrix =
		glm::translate(glm::vec3(0.f, 0.f, distance)) *
		glm::scale(glm::vec3(pie_GetResScalingFactor() / 100.f)) *
		glm::rotate(UNDEG(player->r.z), glm::vec3(0.f, 0.f, 1.f)) *
		glm::rotate(UNDEG(player->r.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(player->r.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::translate(glm::vec3(0, -player->p.y, 0));

	actualCameraPosition = Vector3i(0, 0, 0);

	/* Set the camera position */
	actualCameraPosition.z -= static_cast<int>(distance);

	// Now, scale the world according to what resolution we're running in
	actualCameraPosition.z /= std::max<int>(static_cast<int>(pie_GetResScalingFactor() / 100.f), 1);

	/* Rotate for the player */
	rotateSomething(actualCameraPosition.x, actualCameraPosition.y, -player->r.z);
	rotateSomething(actualCameraPosition.y, actualCameraPosition.z, -player->r.x);
	rotateSomething(actualCameraPosition.z, actualCameraPosition.x, -player->r.y);

	/* Translate */
	actualCameraPosition.y -= -player->p.y;

	// Not sure if should do this here or whenever using, since this transform seems to be done all over the place.
	//actualCameraPosition -= Vector3i(-player->p.x, 0, player->p.z);

	// this also determines the length of the shadows
	const Vector3f theSun = (viewMatrix * glm::vec4(getTheSun(), 0.f)).xyz();
	pie_BeginLighting(theSun);

	// update the fog of war... FIXME: Remove this
	for (int i = -visibleTiles.y / 2, idx = 0; i <= visibleTiles.y / 2; i++, ++idx)
	{
		/* Go through the x's */
		for (int j = -visibleTiles.x / 2, jdx = 0; j <= visibleTiles.x / 2; j++, ++jdx)
		{
			Vector2i screen(0, 0);
			Position pos;

			pos.x = world_coord(j);
			pos.z = -world_coord(i);
			pos.y = 0;

			if (tileOnMap(playerXTile + j, playerZTile + i)) {
				auto psTile = mapTile(playerXTile + j, playerZTile + i);
				pos.y = map_TileHeight(playerXTile + j, playerZTile + i);
				setTileColour(playerXTile + j, playerZTile + i, pal_SetBrightness(static_cast<UBYTE>(psTile->level)));
			}
			tileScreenInfo[idx][jdx].z = pie_RotateProject(&pos, viewMatrix, &screen);
			tileScreenInfo[idx][jdx].x = screen.x;
			tileScreenInfo[idx][jdx].y = screen.y;
		}
	}

	// Determine whether each tile in the drawable range is actually visible on-screen
	// (used for more accurate clipping elsewhere)
	for (int idx = 0; idx < visibleTiles.y; ++idx)
	{
		for (int jdx = 0; jdx < visibleTiles.x; ++jdx)
		{
			QUAD quad;

			quad.coords[0].x = tileScreenInfo[idx + 0][jdx + 0].x;
			quad.coords[0].y = tileScreenInfo[idx + 0][jdx + 0].y;

			quad.coords[1].x = tileScreenInfo[idx + 0][jdx + 1].x;
			quad.coords[1].y = tileScreenInfo[idx + 0][jdx + 1].y;

			quad.coords[2].x = tileScreenInfo[idx + 1][jdx + 1].x;
			quad.coords[2].y = tileScreenInfo[idx + 1][jdx + 1].y;

			quad.coords[3].x = tileScreenInfo[idx + 1][jdx + 0].x;
			quad.coords[3].y = tileScreenInfo[idx + 1][jdx + 0].y;

			tileScreenVisible[idx][jdx] = quadIntersectsWithScreen(quad);
		}
	}

	wzPerfEnd(PERF_START_FRAME);

	/* This is done here as effects can light the terrain - pause mode problems though */
	wzPerfBegin(PERF_EFFECTS, "3D scene - effects");
	processEffects(viewMatrix);
	atmosUpdateSystem();
	avUpdateTiles();
	wzPerfEnd(PERF_EFFECTS);

	// now we are about to draw the terrain
	wzPerfBegin(PERF_TERRAIN, "3D scene - terrain");
	pie_SetFogStatus(true);

	// draw it
	// and draw it
	drawTerrain(pie_PerspectiveGet() * viewMatrix * glm::translate(glm::vec3(-player->p.x, 0, player->p.z)));

	wzPerfEnd(PERF_TERRAIN);

	// draw skybox
	wzPerfBegin(PERF_SKYBOX, "3D scene - skybox");
	renderSurroundings(viewMatrix);
	wzPerfEnd(PERF_SKYBOX);

	// and prepare for rendering the models
	wzPerfBegin(PERF_MODEL_INIT, "Draw 3D scene - model init");

	/* Now display all the static objects                               */
  
	displayStaticObjects(viewMatrix); // may be bucket render implemented
	displayFeatures(viewMatrix);
	displayDynamicObjects(viewMatrix); // may be bucket render implemented
	if (doWeDrawProximitys()) {
		displayProximityMsgs(viewMatrix);
	}
	displayDelivPoints(viewMatrix);
	display3DProjectiles(viewMatrix); // may be bucket render implemented
	wzPerfEnd(PERF_MODEL_INIT);

	wzPerfBegin(PERF_PARTICLES, "3D scene - particles");
	atmosDrawParticles(viewMatrix);
	wzPerfEnd(PERF_PARTICLES);

	wzPerfBegin(PERF_WATER, "3D scene - water");
	// prepare for the water and the lightmap
	pie_SetFogStatus(true);

	// also, make sure we can use world coordinates directly
	drawWater(pie_PerspectiveGet() * viewMatrix * glm::translate(glm::vec3(-player->p.x, 0, player->p.z)));
	wzPerfEnd(PERF_WATER);

	wzPerfBegin(PERF_MODELS, "3D scene - models");
	bucketRenderCurrentList(viewMatrix);

	gfx_api::context::get().debugStringMarker("Draw 3D scene - blueprints");
	displayBlueprints(viewMatrix);

	pie_RemainingPasses(currentGameFrame); // draws shadows and transparent shapes

	if (!gamePaused())
	{
		doConstructionLines(viewMatrix);
	}
	locateMouse();

	wzPerfEnd(PERF_MODELS);
}

/// Initialise the fog, skybox and some other stuff
bool init3DView()
{
	setTheSun(getDefaultSunPosition());

	/* There are no drag boxes */
	dragBox3D.status = DRAG_INACTIVE;

	/* Get all the init stuff out of here? */
	initWarCam();

	/* Init the game messaging system */
	initConsoleMessages();

	atmosInitSystem();

	// default skybox, will override in script if not satisfactory
	setSkyBox("texpages/page-25-sky-arizona.png", 0.0f, 10000.0f);

	// distance is not saved, so initialise it now
	distance = war_GetMapZoom(); // distance

	if (pie_GetFogEnabled())
	{
		pie_SetFogStatus(true);
	}

	// Set the initial fog distance
	updateFogDistance(distance);

	if (strcmp(tilesetDir, "texpages/tertilesc2hw") == 0) // Urban = 0x101040 (or, 0xc9920f)
	{
		PIELIGHT WZCOL_FOG_URBAN;
		WZCOL_FOG_URBAN.vector[0] = 0x10;
		WZCOL_FOG_URBAN.vector[1] = 0x10;
		WZCOL_FOG_URBAN.vector[2] = 0x40;
		WZCOL_FOG_URBAN.vector[3] = 0xff;
		pie_SetFogColour(WZCOL_FOG_URBAN);
	}
	else if (strcmp(tilesetDir, "texpages/tertilesc3hw") == 0) // Rockies = 0xb6e1ec
	{
		PIELIGHT WZCOL_FOG_ROCKIE;
		WZCOL_FOG_ROCKIE.vector[0] = 0xb6;
		WZCOL_FOG_ROCKIE.vector[1] = 0xe1;
		WZCOL_FOG_ROCKIE.vector[2] = 0xec;
		WZCOL_FOG_ROCKIE.vector[3] = 0xff;
		pie_SetFogColour(WZCOL_FOG_ROCKIE);
	}
	else // Arizona, eg. strcmp(tilesetDir, "texpages/tertilesc1hw") == 0, and default. = b08f5f (or, 0x78684f)
	{
		PIELIGHT WZCOL_FOG_ARIZONA;
		WZCOL_FOG_ARIZONA.vector[0] = 0xb0;
		WZCOL_FOG_ARIZONA.vector[1] = 0x8f;
		WZCOL_FOG_ARIZONA.vector[2] = 0x5f;
		WZCOL_FOG_ARIZONA.vector[3] = 0xff;
		pie_SetFogColour(WZCOL_FOG_ARIZONA);
	}

	playerPos.r.z = 0; // roll
	playerPos.r.y = 0; // rotation
	playerPos.r.x = DEG(360 + INITIAL_STARTING_PITCH); // angle

	if (!initTerrain()) {
		return false;
	}

	txtLevelName.setText(mapNameWithoutTechlevel(getLevelName()), font_small);
	txtDebugStatus.setText("DEBUG ", font_small);
	return true;
}

void shutdown3DView()
{
	txtLevelName = WzText();
	txtDebugStatus = WzText();
	txtCurrentTime = WzText();
	txtShowFPS = WzText();
	txtUnits = WzText();
	// show Samples text
	txtShowSamples_Que = WzText();
	txtShowSamples_Lst = WzText();
	txtShowSamples_Act = WzText();
	// show Orders text
	txtShowOrders = WzText();
	// show Droid visible/draw counts text
	droidText = WzText();
}

/// set the view position from save game
void disp3d_setView(iView* newView)
{
	playerPos = *newView;
}

/// reset the camera rotation (used for save games <= 10)
void disp3d_oldView()
{
	playerPos.r.y = OLD_INITIAL_ROTATION; // rotation
	playerPos.p.y = OLD_START_HEIGHT; // height
}

/// get the view position for save game
void disp3d_getView(iView* newView)
{
	*newView = playerPos;
}

/// Are the current world coordinates within the processed range of tiles on the screen?
/// (Warzone has a maximum range of tiles around the current player camera position that it will display.)
bool quickClipXYToMaximumTilesFromCurrentPosition(int x, int y)
{
	// +2 for edge of visibility fading (see terrain.cpp)
	if (std::abs(x - playerPos.p.x) < world_coord(visibleTiles.x / 2 + 2) &&
		std::abs(y - playerPos.p.z) < world_coord(visibleTiles.y / 2 + 2)) {
		return true;
	}
	else {
		return false;
	}
}

/// Are the current tile coordinates visible on screen?
bool clipXY(int x, int y)
{
	// +2 for edge of visibility fading (see terrain.cpp)
	if (std::abs(x - playerPos.p.x) < world_coord(visibleTiles.x / 2 + 2) &&
		std::abs(y - playerPos.p.z) < world_coord(visibleTiles.y / 2 + 2)) {
		// additional check using the tileScreenVisible matrix
		auto mapX = map_coord(x - playerPos.p.x) + visibleTiles.x / 2;
		auto mapY = map_coord(y - playerPos.p.z) + visibleTiles.y / 2;
    
		if (mapX < 0 || mapY < 0) {
			return false;
		}
		if (mapX > visibleTiles.x || mapY > visibleTiles.y) {
			return false;
		}
		return tileScreenVisible[mapY][mapX];
	}
	else {
		return false;
	}
}

bool clipXYZNormalized(const Vector3i& normalizedPosition, const glm::mat4& viewMatrix)
{
	Vector2i pixel(0, 0);
	pie_RotateProject(&normalizedPosition, viewMatrix, &pixel);
  
	return pixel.x >= 0 && pixel.y >= 0 && 
         pixel.x < pie_GetVideoBufferWidth() && 
         pixel.y < pie_GetVideoBufferHeight();
}

/// Are the current 3d game-world coordinates visible on screen?
/// (Does not take into account occlusion)
bool clipXYZ(int x, int y, int z, const glm::mat4& viewMatrix)
{
	Vector3i position;
	position.x = x - playerPos.p.x;
	position.z = -(y - playerPos.p.z);
	position.y = z;

	return clipXYZNormalized(position, viewMatrix);
}

bool clipShapeOnScreen(const iIMDShape* pIMD, const glm::mat4& viewModelMatrix, int overdrawScreenPoints /*= 10*/)
{
	/* Get its absolute dimensions */
	Vector3i origin;
	Vector2i center(0, 0);
	int wsRadius = 22; // World space radius, 22 = magic minimum
	float radius;

	if (pIMD) {
		wsRadius = MAX(wsRadius, pIMD->radius);
	}

	origin = Vector3i(0, wsRadius, 0); // take the center of the object

	/* get the screen coordinates */
	const float cZ = pie_RotateProject(&origin, viewModelMatrix, &center) * 0.1f;

	// avoid division by zero
	if (cZ > 0)
	{
		radius = wsRadius / cZ * pie_GetResScalingFactor();
	}
	else
	{
		radius = 1; // 1 just in case some other code assumes radius != 0
	}

	const int screenMinX = -overdrawScreenPoints;
	const int screenMinY = -overdrawScreenPoints;

	return (center.x + radius > screenMinX &&
		center.x - radius < (pie_GetVideoBufferWidth() + overdrawScreenPoints) &&
		center.y + radius > screenMinY &&
		center.y - radius < (pie_GetVideoBufferHeight() + overdrawScreenPoints));
}

// Use overdrawScreenPoints as a workaround for casting shadows when the main unit is off-screen (but right at the edge)
bool clipDroidOnScreen(Droid* psDroid, const glm::mat4& viewModelMatrix, int overdrawScreenPoints /*= 25*/)
{
	/* Get its absolute dimensions */
	// NOTE: This only takes into account body, but is "good enough"
	const auto psBStats = psDroid->getComponent(COMPONENT_TYPE::BODY);
	const auto pIMD = (psBStats != nullptr) ? psBStats->pIMD.get() : nullptr;
	return clipShapeOnScreen(pIMD, viewModelMatrix, overdrawScreenPoints);
}

bool clipStructureOnScreen(Structure const* psStructure)
{
	auto const& b = getStructureBounds(psStructure);
	assert(b.size.x != 0 && b.size.y != 0);
	for (auto breadth = 0; breadth < b.size.y + 2; ++breadth) // +2 to make room for shadows on the terrain
	{
		for (auto width = 0; width < b.size.x + 2; ++width)
		{
			if (clipXY(world_coord(b.map.x + width),
                 world_coord(b.map.y + breadth))) {
				return true;
			}
		}
	}

	return false;
}

/** 
 * Get the screen coordinates for the current transform matrix.
 * This function is used to determine the area the user can click for the
 * intelligence screen buttons. The radius parameter is always set to the same value.
 */
static void calcFlagPosScreenCoords(int* pX, int* pY, int* pR, const glm::mat4& modelViewMatrix)
{
	/* Get its absolute dimensions */
	Vector3i center3d(0, 0, 0);
	Vector2i center2d(0, 0);
  
	/* How big a box do we want - will ultimately be calculated using xmax, ymax, zmax etc */
	unsigned radius = 22;

	/* Pop matrices and get the screen coordinates for last point*/
	pie_RotateProject(&center3d, modelViewMatrix, &center2d);

	/*store the coords*/
	*pX = center2d.x;
	*pY = center2d.y;
	*pR = radius;
}

/// Decide whether to render a projectile, and make sure it will be drawn
static void display3DProjectiles(const glm::mat4& viewMatrix)
{
	auto psObj = proj_GetFirst();
	while (psObj != nullptr)
	{
		// If source or destination is visible, and projectile has been spawned and has not impacted.
		if (graphicsTime >= psObj->getPreviousLocation().time &&
        graphicsTime <= psObj->getTime() && gfxVisible(psObj)) {
			/* Draw a bullet at psObj->pos.x for X coord
			   psObj->pos.y for Z coord
			   whatever for Y (height) coord - arcing ?
			*/
			/* these guys get drawn last */
			if (psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::ROCKET ||
          psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::MISSILE ||
          psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::COMMAND ||
          psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::SLOW_MISSILE ||
          psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::SLOW_ROCKET ||
          psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::ENERGY ||
          psObj->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::EMP) {
				bucketAddTypeToList(RENDER_TYPE::RENDER_PROJECTILE, psObj, viewMatrix);
			}
			else {
				renderProjectile(psObj, viewMatrix);
			}
		}
		psObj = proj_GetNext();
	}
} /* end of function display3DProjectiles */

/// Draw a projectile to the screen
void renderProjectile(Projectile* psCurr, const glm::mat4& viewMatrix)
{
	Vector3i dv;
	iIMDShape* pIMD;
	Spacetime st;

	auto psStats = psCurr->getWeaponStats();
	/* Reject flame or command since they have interim drawn fx */
	if (psStats->weaponSubClass == WEAPON_SUBCLASS::FLAME ||
      psStats->weaponSubClass == WEAPON_SUBCLASS::COMMAND ||// || psStats->weaponSubClass == WEAPON_SUBCLASS::ENERGY)
      psStats->weaponSubClass == WEAPON_SUBCLASS::ELECTRONIC ||
      psStats->weaponSubClass == WEAPON_SUBCLASS::EMP ||
      bMultiPlayer && psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
		// we don't do projectiles from these guys, cos there's an effect instead
		return;
	}

	st = interpolateObjectSpacetime(psCurr, graphicsTime);

	//the weapon stats holds the reference to which graphic to use
	/*Need to draw the graphic depending on what the projectile is doing - hitting target,
	missing target, in flight etc - JUST DO IN FLIGHT FOR NOW! */
	pIMD = psStats->pInFlightGraphic.get();

	if (!clipXYZ(st.position.x, st.position.y, 
               st.position.z, viewMatrix)) {
		return;
		// projectile is not on the screen (Note: This uses the position point
    // of the projectile, not a full shape clipping check, for speed)
	}
	for (; pIMD != nullptr; pIMD = pIMD->next)
	{
		bool rollToCamera = false;
		bool pitchToCamera = false;
		bool premultiplied = false;
		bool additive = psStats->weaponSubClass == WEAPON_SUBCLASS::ROCKET ||
            psStats->weaponSubClass == WEAPON_SUBCLASS::MISSILE ||
            psStats->weaponSubClass == WEAPON_SUBCLASS::SLOW_ROCKET ||
            psStats->weaponSubClass == WEAPON_SUBCLASS::SLOW_MISSILE;

		if (pIMD->flags & iV_IMD_ROLL_TO_CAMERA) {
			rollToCamera = true;
		}
		if (pIMD->flags & iV_IMD_PITCH_TO_CAMERA) {
			rollToCamera = true;
			pitchToCamera = true;
		}
		if (pIMD->flags & iV_IMD_NO_ADDITIVE) {
			additive = false;
		}
		if (pIMD->flags & iV_IMD_ADDITIVE) {
			additive = true;
		}
		if (pIMD->flags & iV_IMD_PREMULTIPLIED) {
			additive = false;
			premultiplied = true;
		}

		/* Get bullet's x coord */
		dv.x = st.position.x - playerPos.p.x;

		/* Get it's y coord (z coord in the 3d world */
		dv.z = -(st.position.y - playerPos.p.z);

		/* What's the present height of the bullet? */
		dv.y = st.position.z;
		/* Set up the matrix */
		Vector3i camera = actualCameraPosition;

		/* Translate to the correct position */
		camera -= dv;

		/* Rotate it to the direction it's facing */
		rotateSomething(camera.z, camera.x, -(-st.rotation.direction));

		/* pitch it */
		rotateSomething(camera.y, camera.z, -st.rotation.pitch);

		glm::mat4 modelMatrix =
			glm::translate(glm::vec3(dv)) *
		  	glm::rotate(UNDEG(-st.rotation.direction),
                  glm::vec3(0.f, 1.f, 0.f)) *
		  	glm::rotate(UNDEG(st.rotation.pitch),
                  glm::vec3(1.f, 0.f, 0.f));

		if (pitchToCamera || rollToCamera) {
			// Centre on projectile (relevant for twin projectiles).
			camera -= Vector3i(pIMD->connectors[0].x, pIMD->connectors[0].y, pIMD->connectors[0].z);
			modelMatrix *= glm::translate(glm::vec3(pIMD->connectors[0]));
		}

		if (pitchToCamera) {
			int x = iAtan2(camera.z, camera.y);
			rotateSomething(camera.y, camera.z, -x);
			modelMatrix *= glm::rotate(UNDEG(x), glm::vec3(1.f, 0.f, 0.f));
		}

		if (rollToCamera) {
			int z = -iAtan2(camera.x, camera.y);
			rotateSomething(camera.x, camera.y, -z);
			modelMatrix *= glm::rotate(UNDEG(z), glm::vec3(0.f, 0.f, 1.f));
		}

		if (pitchToCamera || rollToCamera) {
			camera -= Vector3i(-pIMD->connectors[0].x, -pIMD->connectors[0].y, -pIMD->connectors[0].z);
			// Undo centre on projectile (relevant for twin projectiles).
			modelMatrix *= glm::translate(glm::vec3(-pIMD->connectors[0].x, -pIMD->connectors[0].y,
			                                        -pIMD->connectors[0].z));
		}

		if (premultiplied)
		{
			pie_Draw3DShape(pIMD, 0, 0, WZCOL_WHITE, pie_PREMULTIPLIED, 0, viewMatrix * modelMatrix);
		}
		else if (additive)
		{
			pie_Draw3DShape(pIMD, 0, 0, WZCOL_WHITE, pie_ADDITIVE, 164, viewMatrix * modelMatrix);
		}
		else
		{
			pie_Draw3DShape(pIMD, 0, 0, WZCOL_WHITE, 0, 0, viewMatrix * modelMatrix);
		}
	}
}

/// Draw the buildings
static void displayStaticObjects(const glm::mat4& viewMatrix)
{
	// to solve the flickering edges of baseplates
	//	pie_SetDepthOffset(-1.0f);

	/* Go through all the players */
	for (unsigned aPlayer = 0; aPlayer <= MAX_PLAYERS; ++aPlayer)
	{
    BaseObject * list = aPlayer < MAX_PLAYERS
            ? playerList[aPlayer].structures
            : psDestroyedObj;

		/* Now go all buildings for that player */
		for (; list != nullptr; list = list->psNext)
		{
			/* Worth rendering the structure? */
			if (getObjectType(list) != OBJECT_TYPE::STRUCTURE ||
          (list->damageManager->isDead() != 0 &&
           list->damageManager->isDead() < graphicsTime)) {
				continue;
			}
			auto psStructure = dynamic_cast<Structure*>(list);

			if (!clipStructureOnScreen(psStructure)) {
				continue;
			}

			renderStructure(psStructure, viewMatrix);
		}
	}
	//	pie_SetDepthOffset(0.0f);
}

static bool tileHasIncompatibleStructure(Tile const* tile, StructureStats const* stats, int moduleIndex)
{
	auto psStruct = dynamic_cast<Structure*>(tile->psObject);
	if (!psStruct) {
		return false;
	}
	if (psStruct->getState() == STRUCTURE_STATE::BEING_BUILT &&
     nextModuleToBuild(psStruct, -1) >= moduleIndex) {
		return true;
	}
	if (isWall(psStruct->getStats()->type) &&
      isBuildableOnWalls(stats->type)) {
		return false;
	}
	if (IsStatExpansionModule(stats)) {
		return false;
	}
	return true;
}

static void drawLineBuild(uint8_t player, StructureStats const* psStats,
                          Vector2i pos, Vector2i pos2, uint16_t direction,
                          STRUCTURE_STATE state)
{
	auto lb = calcLineBuild(psStats, direction, pos, pos2);

	for (int i = 0; i < lb.count; ++i)
	{
		Vector2i cur = lb[i];
		if (tileHasIncompatibleStructure(worldTile(cur), psStats, 0))
		{
			continue; // construction has started
		}

		StructureBounds b = getStructureBounds(psStats, cur, direction);
		int z = 0;
		for (int j = 0; j <= b.size.y; ++j)
			for (int k = 0; k <= b.size.x; ++k)
			{
				z = std::max(z, map_TileHeight(b.map.x + k, b.map.y + j));
			}
		Blueprint blueprint(psStats, Vector3i(cur, z), snapDirection(direction), 0, state, player);
		// snapDirection may be unnecessary here
		blueprints.push_back(blueprint);
	}
}

static void renderBuildOrder(uint8_t droidPlayer, Order const& order, STRUCTURE_STATE state)
{
	StructureStats const* stats;
	Vector2i pos = order.pos;
	if (order.type == ORDER_TYPE::BUILD_MODULE) {
		auto const structure = dynamic_cast<Structure*>(order.target);
		if (!structure) {
			return;
		}
		stats = getModuleStat(structure);
		pos = structure->getPosition().xy();
	}
	else {
		stats = order.structure_stats.get();
	}

	if (!stats) {
		return;
	}

	//draw the current build site if its a line of structures
	if (order.type == ORDER_TYPE::LINE_BUILD) {
		drawLineBuild(droidPlayer, stats, pos,
                  order.pos2, order.direction, state);
	}
	if ((order.type == ORDER_TYPE::BUILD ||
       order.type == ORDER_TYPE::BUILD_MODULE) &&
      !tileHasIncompatibleStructure(
		mapTile(map_coord(pos)), stats, order.index)) {

		auto b = getStructureBounds(stats, pos, order.direction);
		int z = 0;
		for (int j = 0; j <= b.size.y; ++j)
			for (int i = 0; i <= b.size.x; ++i)
			{
				z = std::max(z, map_TileHeight(b.map.x + i, b.map.y + j));
			}
		Blueprint blueprint(stats, Vector3i(pos, z),
                        snapDirection(order.direction),
                        order.index, state, droidPlayer);

		blueprints.push_back(blueprint);
	}
}

std::unique_ptr<Blueprint> playerBlueprint = std::make_unique<Blueprint>();
std::unique_ptr<ValueTracker> playerBlueprintX = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> playerBlueprintY = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> playerBlueprintZ = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> playerBlueprintDirection = std::make_unique<ValueTracker>();

void displayBlueprints(const glm::mat4& viewMatrix)
{
	blueprints.clear(); // Delete old blueprints and draw new ones.

	if ((buildState == BUILD3D_VALID || buildState == BUILD3D_POS) &&
		sBuildDetails.x > 0 && sBuildDetails.x < (int)mapWidth &&
		sBuildDetails.y > 0 && sBuildDetails.y < (int)mapHeight) {
		STRUCTURE_STATE state;
		if (buildState == BUILD3D_VALID) {
			state = STRUCTURE_STATE::BLUEPRINT_VALID;
		}
		else {
			state = STRUCTURE_STATE::BLUEPRINT_INVALID;
		}
		// we are placing a building or a delivery point
		if (auto stats = dynamic_cast<StructureStats*>(sBuildDetails.psStats)) {
			// it's a building
			auto direction = getBuildingDirection();
			if (wallDrag.status == DRAG_PLACING || wallDrag.status == DRAG_DRAGGING) {
				drawLineBuild(selectedPlayer, stats, wallDrag.pos, wallDrag.pos2, direction, state);
			}
			else {
				unsigned width, height;
				if ((direction & 0x4000) == 0) {
					width = sBuildDetails.width;
					height = sBuildDetails.height;
				}
				else {
					// Rotated 90°, swap width and height
					width = sBuildDetails.height;
					height = sBuildDetails.width;
				}
				// a single building
				Vector2i pos(world_coord(sBuildDetails.x) + world_coord(width) / 2,
				             world_coord(sBuildDetails.y) + world_coord(height) / 2);

				auto b = getStructureBounds(stats, pos, direction);
				int z = 0;
				for (int j = 0; j <= b.size.y; ++j)
					for (int i = 0; i <= b.size.x; ++i)
					{
						z = std::max(z, map_TileHeight(b.map.x + i, b.map.y + j));
					}

				if (!playerBlueprintX->isTracking()) {
          playerBlueprintX->start(pos.x);
          playerBlueprintX->start(BlueprintTrackAnimationSpeed);
          playerBlueprintY->start(pos.y);
          playerBlueprintY->start(BlueprintTrackAnimationSpeed);
          playerBlueprintZ->start(z);
          playerBlueprintZ->start(BlueprintTrackAnimationSpeed);
          playerBlueprintDirection->start(direction);
          playerBlueprintDirection->start(BlueprintTrackAnimationSpeed + 30);
				}

        playerBlueprintX->set_target(pos.x);
        playerBlueprintX->update();
        playerBlueprintY->set_target(pos.y);
        playerBlueprintY->update();
        playerBlueprintZ->set_target(z);
        playerBlueprintZ->update();

				if (playerBlueprintDirection->reachedTarget()) {
          playerBlueprintDirection->start(playerBlueprintDirection->get_target());
          playerBlueprintDirection->
                  set_target_delta((SWORD) (direction - playerBlueprintDirection->get_target()));
				}

				playerBlueprintDirection->update();

				playerBlueprint->stats = stats;
				playerBlueprint->pos = {
                playerBlueprintX->get_current(), playerBlueprintY->get_current(), playerBlueprintZ->get_current()
				};
				playerBlueprint->dir = playerBlueprintDirection->get_current();
				playerBlueprint->index = 0;
				playerBlueprint->state = state;
				playerBlueprint->player = selectedPlayer;

				blueprints.push_back(*playerBlueprint);
			}
		}
	}
	else
	{
    playerBlueprintX->stop();
    playerBlueprintY->stop();
    playerBlueprintZ->stop();
    playerBlueprintDirection->stop();
	}

	// now we draw the blueprints for all ordered buildings
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		if (!hasSharedVision(selectedPlayer, player) &&
        !NetPlay.players[selectedPlayer].isSpectator) {
			continue;
		}
		auto state = player == selectedPlayer
            ? STRUCTURE_STATE::BLUEPRINT_PLANNED
            : STRUCTURE_STATE::BLUEPRINT_PLANNED_BY_ALLY;

		for (auto const& psDroid : playerList[player].droids)
		{
			if (psDroid.getType() == DROID_TYPE::CONSTRUCT ||
          psDroid.getType() == DROID_TYPE::CYBORG_CONSTRUCT) {
				renderBuildOrder(psDroid.playerManager->getPlayer(), *psDroid.getOrder(), state);
				//now look thru' the list of orders to see if more building sites
				for (auto order = psDroid.listPendingBegin; order < (int)psDroid.asOrderList.size(); order++)
				{
					renderBuildOrder(psDroid.playerManager->getPlayer(), psDroid.asOrderList[order], state);
				}
			}
		}
	}
	// erase duplicate blueprints.
	std::sort(blueprints.begin(), blueprints.end());
	blueprints.erase(std::unique(blueprints.begin(), blueprints.end()), blueprints.end());

	// actually render everything.
	for (auto& blueprint : blueprints)
	{
		blueprint.renderBlueprint(viewMatrix);
	}
	renderDeliveryRepos(viewMatrix);
}

/// Draw Factory Delivery Points
static void displayDelivPoints(const glm::mat4& viewMatrix)
{
	if (selectedPlayer >= MAX_PLAYERS) {
    return; // no-op
  }
	for (auto& psDelivPoint : playerList[selectedPlayer].flagPositions)
	{
		if (clipXY(psDelivPoint.coords.x, psDelivPoint.coords.y)) {
			renderDeliveryPoint(&psDelivPoint, false, viewMatrix);
		}
	}
}

/// Draw the features
static void displayFeatures(const glm::mat4& viewMatrix)
{
	// player can only be 0 for the features.
	for (auto player = 0; player <= 1; ++player)
	{
    BaseObject* list = player < 1 ? apsFeatureLists[player] : psDestroyedObj;

		/* Go through all the features */
		for (; list != nullptr; list = list->psNext)
		{
			if (getObjectType(list) == OBJECT_TYPE::FEATURE
				  && (list->damageManager->isDead() == 0 || list->damageManager->isDead() > graphicsTime)
				  && clipXY(list->getPosition().x, list->getPosition().y)) {
				auto psFeature = dynamic_cast<Feature*>(list);
				renderFeature(psFeature, viewMatrix);
			}
		}
	}
}

/// Draw the Proximity messages for the *SELECTED PLAYER ONLY*
static void displayProximityMsgs(const glm::mat4& viewMatrix)
{
	if (selectedPlayer >= MAX_PLAYERS) {
    return; /* no-op */
  }

	/* Go through all the proximity Displays*/
	for (PROXIMITY_DISPLAY* psProxDisp = apsProxDisp[selectedPlayer]; psProxDisp != nullptr; psProxDisp = psProxDisp->
	     psNext)
	{
    if (psProxDisp->psMessage->read) {
      continue;
    }
    unsigned x, y;
    if (psProxDisp->type == POSITION_TYPE::POS_PROXDATA) {
      auto pViewProximity = (VIEW_PROXIMITY*)psProxDisp->psMessage->pViewData->pData;
      x = pViewProximity->x;
      y = pViewProximity->y;
    }
    else {
      if (!psProxDisp->psMessage->psObj) {
        continue; // sanity check
      }
      x = psProxDisp->psMessage->psObj->getPosition().x;
      y = psProxDisp->psMessage->psObj->getPosition().y;
    }
    /* Is the Message worth rendering? */
    if (clipXY(x, y)) {
      renderProximityMsg(psProxDisp, viewMatrix);
    }
  }
}

/// Draw the droids
static void displayDynamicObjects(const glm::mat4& viewMatrix)
{
	/* Need to go through all the droid lists */
	for (auto player = 0; player <= MAX_PLAYERS; ++player)
	{
    BaseObject* list = player < MAX_PLAYERS ? playerList[player].droids : psDestroyedObj;

		for (; list != nullptr; list = list->psNext)
		{
			auto psDroid = dynamic_cast<Droid*>(list);
			if (!psDroid || (list->damageManager->getTimeOfDeath() != 0 &&
                       list->damageManager->getTimeOfDeath() < graphicsTime)
				|| !quickClipXYToMaximumTilesFromCurrentPosition(
                list->getPosition().x,
                list->getPosition().y)) {
				continue;
			}

			/* No point in adding it if you can't see it? */
			if (psDroid->isVisibleToSelectedPlayer()) {
				displayComponentObject(psDroid, viewMatrix);
			}
		}
	}
}

/// Sets the player's position and view angle - defaults player rotations as well
void setViewPos(unsigned x, unsigned y, WZ_DECL_UNUSED bool Pan)
{
	playerPos.p.x = world_coord(x);
	playerPos.p.z = world_coord(y);
	playerPos.r.z = 0;

	updatePlayerAverageCentreTerrainHeight();

	if (playerPos.p.y < averageCentreTerrainHeight) {
		playerPos.p.y = averageCentreTerrainHeight +
            CAMERA_PIVOT_HEIGHT - HEIGHT_TRACK_INCREMENTS;
	}

	if (getWarCamStatus()) {
		camToggleStatus();
	}
}

/// Get the player position
Vector2i getPlayerPos()
{
	return playerPos.p.xz();
}

/// Set the player position
void setPlayerPos(int x, int y)
{
	ASSERT(x >= 0 && x < world_coord(mapWidth) &&
         y >= 0 && y < world_coord(mapHeight),
         "Position off map");

	playerPos.p.x = x;
	playerPos.p.z = y;
	playerPos.r.z = 0;
}

/// Get the distance at which the player views the world
float getViewDistance()
{
	return distance;
}

/// Set the distance at which the player views the world
void setViewDistance(float dist)
{
	distance = dist;
	debug(LOG_WZ, _("Setting zoom to %.0f"), distance);
}

/// Draw a feature (tree/rock/etc.)
void renderFeature(Feature* psFeature, const glm::mat4& viewMatrix)
{
	auto brightness = pal_SetBrightness(200);
	auto bForceDraw = (getRevealStatus() && psFeature->getStats()->visibleAtStart);
	int pieFlags = 0;

	if (!psFeature->isVisibleToSelectedPlayer() && !bForceDraw) {
		return;
	}

	/* Mark it as having been drawn */
	psFeature->getDisplayData()->frame_number = currentGameFrame;

	/* Daft hack to get around the oil derrick issue */
	if (!TileHasFeature(mapTile(map_coord(
          psFeature->getPosition().xy())))) {
		return;
	}

	Vector3i dv = Vector3i(
		psFeature->getPosition().x - playerPos.p.x,
		psFeature->getPosition().z, // features sits at the height of the tile it's centre is on
		-(psFeature->getPosition().y - playerPos.p.z)
	);

	glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) * glm::rotate(UNDEG(-psFeature->getRotation().direction),
	                                                                    glm::vec3(0.f, 1.f, 0.f));

	if (psFeature->getStats()->subType == FEATURE_TYPE::SKYSCRAPER)
	{
		modelMatrix *= objectShimmy((BaseObject *)psFeature);
	}

	if (!getRevealStatus())
	{
		brightness = pal_SetBrightness(avGetObjLightLevel((BaseObject *)psFeature, brightness.byte.r));
	}
	if (!hasSensorOnTile(mapTile(map_coord(psFeature->getPosition().x), map_coord(psFeature->getPosition().y)), selectedPlayer))
	{
		brightness.byte.r /= 2;
		brightness.byte.g /= 2;
		brightness.byte.b /= 2;
	}

	if (psFeature->getStats()->subType == FEATURE_TYPE::BUILDING
		  || psFeature->getStats()->subType == FEATURE_TYPE::SKYSCRAPER
		  || psFeature->getStats()->subType == FEATURE_TYPE::GEN_ARTE
		  || psFeature->getStats()->subType == FEATURE_TYPE::BOULDER
		  || psFeature->getStats()->subType == FEATURE_TYPE::VEHICLE
		  || psFeature->getStats()->subType == FEATURE_TYPE::OIL_DRUM) {
		/* these cast a shadow */
		pieFlags = pie_SHADOW;
	}
	auto& imd = psFeature->getDisplayData()->imd_shape;
	while (imd)
	{
		/* Translate the feature  - N.B. We can also do rotations here should we require
		buildings to face different ways - Don't know if this is necessary - should be IMO */
		pie_Draw3DShape(imd.get(), 0, 0, brightness, pieFlags, 0, viewMatrix * modelMatrix);
		imd = imd->next;
	}

	setScreenDisp(psFeature->getDisplayData(), viewMatrix * modelMatrix);
}

void renderProximityMsg(PROXIMITY_DISPLAY* psProxDisp, const glm::mat4& viewMatrix)
{
	unsigned msgX = 0, msgY = 0;
	Vector3i dv(0, 0, 0);
	VIEW_PROXIMITY* pViewProximity = nullptr;
	int x, y, r;
	iIMDShape* proxImd = nullptr;

	//store the frame number for when deciding what has been clicked on
	psProxDisp->frameNumber = currentGameFrame;

	/* Get it's x and y coordinates so we don't have to deref. struct later */
	if (psProxDisp->type == POSITION_TYPE::POS_PROXDATA)
	{
		pViewProximity = (VIEW_PROXIMITY*)psProxDisp->psMessage->pViewData->pData;
		if (pViewProximity)
		{
			msgX = pViewProximity->x;
			msgY = pViewProximity->y;
			/* message sits at the height specified at input*/
			dv.y = pViewProximity->z + 64;

			/* in case of a beacon message put above objects */
			if (psProxDisp->psMessage->pViewData->type == VIEW_TYPE::VIEW_BEACON)
			{
				if (TileIsOccupied(mapTile(msgX / TILE_UNITS, msgY / TILE_UNITS)))
				{
					dv.y = pViewProximity->z + 150;
				}
			}
		}
	}
	else if (psProxDisp->type == POSITION_TYPE::POS_PROXOBJ)
	{
		msgX = psProxDisp->psMessage->psObj->getPosition().x;
		msgY = psProxDisp->psMessage->psObj->getPosition().y;
		/* message sits at the height specified at input*/
		dv.y = psProxDisp->psMessage->psObj->getPosition().z + 64;
	}
	else
	{
		ASSERT(!"unknown proximity display message type", "Buggered proximity message type");
		return;
	}

	dv.x = msgX - playerPos.p.x;
#if defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4146 ) // warning C4146: unary minus operator applied to unsigned type, result still unsigned
#endif
	dv.z = -(msgY - playerPos.p.z);
#if defined( _MSC_VER )
#pragma warning( pop )
#endif

	/* Translate the message */
	glm::mat4 modelMatrix = glm::translate(glm::vec3(dv));

	//get the appropriate IMD
	if (pViewProximity)
	{
		switch (pViewProximity->proxType)
		{
		case PROX_ENEMY:
			proxImd = getImdFromIndex(MI_BLIP_ENEMY);
			break;
		case PROX_RESOURCE:
			proxImd = getImdFromIndex(MI_BLIP_RESOURCE);
			break;
		case PROX_ARTEFACT:
			proxImd = getImdFromIndex(MI_BLIP_ARTEFACT);
			break;
		default:
			ASSERT(!"unknown proximity display message type", "Buggered proximity message type");
			break;
		}
	}
	else {
		//object Proximity displays are for oil resources and artefacts
		ASSERT_OR_RETURN(, getObjectType(psProxDisp->psMessage->psObj) == OBJECT_TYPE::FEATURE,
		                   "Invalid object type for proximity display");

		if (dynamic_cast<Feature*>(psProxDisp->psMessage->psObj)->
            getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
			//resource
			proxImd = getImdFromIndex(MI_BLIP_RESOURCE);
		}
		else {
			//artefact
			proxImd = getImdFromIndex(MI_BLIP_ARTEFACT);
		}
	}

	modelMatrix *= glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(1.f, 0.f, 0.f));

	if (proxImd) {
		pie_Draw3DShape(proxImd, getModularScaledGraphicsTime(proxImd->animInterval, proxImd->numFrames), 0,
		                WZCOL_WHITE, pie_ADDITIVE, 192, viewMatrix * modelMatrix);
	}
	//get the screen coords for determining when clicked on
	calcFlagPosScreenCoords(&x, &y, &r, viewMatrix * modelMatrix);
	psProxDisp->screenX = x;
	psProxDisp->screenY = y;
	psProxDisp->screenR = r;
}

static PIELIGHT getBlueprintColour(STRUCTURE_STATE state)
{
  using enum STRUCTURE_STATE;
	switch (state) {
	case BLUEPRINT_VALID:
		return WZCOL_LGREEN;
	case BLUEPRINT_INVALID:
		return WZCOL_LRED;
	case BLUEPRINT_PLANNED:
		return WZCOL_BLUEPRINT_PLANNED;
	case BLUEPRINT_PLANNED_BY_ALLY:
		return WZCOL_BLUEPRINT_PLANNED_BY_ALLY;
	default:
		debug(LOG_ERROR, "this is not a blueprint");
		return WZCOL_WHITE;
	}
}

static void renderStructureTurrets(Structure* psStructure, iIMDShape* strImd,
                                   PIELIGHT buildingBrightness, int pieFlag,
                                   int pieFlagData, int ecmFlag, const glm::mat4& modelViewMatrix)
{
	iIMDShape* mountImd[MAX_WEAPONS] = {nullptr};
	iIMDShape* weaponImd[MAX_WEAPONS] = {nullptr};
	iIMDShape* flashImd[MAX_WEAPONS] = {nullptr};

	auto colour = getPlayerColour(psStructure->playerManager->getPlayer());

	//get an imd to draw on the connector priority is weapon, ECM, sensor
	//check for weapon
	for (auto i = 0; i < MAX(1, numWeapons(*psStructure)); i++)
	{
		if (psStructure->asWeaps[i].nStat > 0) {
			const auto nWeaponStat = psStructure->weaponManager->weapons[i].stats.get();

			weaponImd[i] = nWeaponStat->pIMD.get();
			mountImd[i] = nWeaponStat->pMountGraphic.get();
			flashImd[i] = nWeaponStat->pMuzzleGraphic.get();
		}
	}

	// check for ECM
	if (weaponImd[0] == nullptr && psStructure->getStats()->ecm_stats != nullptr)
	{
		weaponImd[0] = psStructure->getStats()->ecm_stats->pIMD.get();
		mountImd[0] = psStructure->getStats()->ecm_stats->pMountGraphic.get();
		flashImd[0] = nullptr;
	}
	// check for sensor (or repair center)
	bool noRecoil = false;
	if (weaponImd[0] == nullptr && psStructure->getStats()->sensor_stats != nullptr)
	{
		weaponImd[0] = psStructure->getStats()->sensor_stats->pIMD.get();
		/* No recoil for sensors */
		noRecoil = true;
		mountImd[0] = psStructure->getStats()->sensor_stats->pMountGraphic.get();
		flashImd[0] = nullptr;
	}

	// flags for drawing weapons
	if (structureIsBlueprint(psStructure))
	{
		pieFlag = pie_TRANSLUCENT;
		pieFlagData = BLUEPRINT_OPACITY;
	}
	else
	{
		pieFlag = pie_SHADOW | ecmFlag;
		pieFlagData = 0;
	}

	// draw Weapon / ECM / Sensor for structure
	for (auto i = 0; i < numWeapons(*psStructure) || i == 0; i++)
	{
		Rotation rot = structureGetInterpolatedWeaponRotation(psStructure, i, graphicsTime);

		if (weaponImd[i] != nullptr)
		{
			glm::mat4 matrix = glm::translate(glm::vec3(strImd->connectors[i].xzy())) * glm::rotate(
				UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));
			auto recoilValue = noRecoil ? 0 : psStructure->weaponManager->weapons[i].getRecoil();
			if (mountImd[i] != nullptr)
			{
				matrix *= glm::translate(glm::vec3(0.f, 0.f, recoilValue / 3.f));
				int animFrame = 0;
				if (mountImd[i]->numFrames > 0) // Calculate an animation frame
				{
					animFrame = getModularScaledGraphicsTime(mountImd[i]->animInterval, mountImd[i]->numFrames);
				}
				pie_Draw3DShape(mountImd[i], animFrame, colour, buildingBrightness, pieFlag, pieFlagData,
				                modelViewMatrix * matrix);
				if (mountImd[i]->nconnectors)
				{
					matrix *= glm::translate(glm::vec3(mountImd[i]->connectors->xzy()));
				}
			}
			matrix *= glm::rotate(UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
			matrix *= glm::translate(glm::vec3(0, 0, recoilValue));

			pie_Draw3DShape(weaponImd[i], 0, colour, buildingBrightness, pieFlag, pieFlagData,
			                modelViewMatrix * matrix);
			if (psStructure->getState() == STRUCTURE_STATE::BUILT &&
          psStructure->isVisibleToSelectedPlayer() > (UBYTE_MAX / 2)) {
				if (psStructure->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY)
				{
					RepairFacility* psRepairFac = &psStructure->pFunctionality->repairFacility;
					// draw repair flash if the Repair Facility has a target which it has started work on
					if (weaponImd[i]->nconnectors && psRepairFac->psObj != nullptr
						&& getObjectType(psRepairFac->psObj) == OBJECT_TYPE::DROID) {
						auto psDroid = (Droid*)psRepairFac->psObj;
						SDWORD xdiff, ydiff;
						xdiff = (SDWORD)psDroid->getPosition().x - (SDWORD)psStructure->getPosition().x;
						ydiff = (SDWORD)psDroid->getPosition().y - (SDWORD)psStructure->getPosition().y;
						if (xdiff * xdiff + ydiff * ydiff <= (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2))
						{
							iIMDShape* pRepImd;
							pRepImd = getImdFromIndex(MI_FLAME);


							matrix *= glm::translate(glm::vec3(weaponImd[i]->connectors->x,
							                                   weaponImd[i]->connectors->z - 12,
							                                   weaponImd[i]->connectors->y)) *
								glm::rotate(UNDEG(rot.direction), glm::vec3(0.f, 1.f, 0.f)) *
								glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)) *
								glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(1.f, 0.f, 0.f));
							pie_Draw3DShape(
								pRepImd, getModularScaledGraphicsTime(pRepImd->animInterval, pRepImd->numFrames),
								colour, buildingBrightness, pie_ADDITIVE, 192, modelViewMatrix * matrix);
						}
					}
				}
				else // we have a weapon so we draw a muzzle flash
				{
					drawMuzzleFlash(psStructure->weaponManager->weapons[i], weaponImd[i], flashImd[i], buildingBrightness, pieFlag,
					                pieFlagData, modelViewMatrix * matrix, colour);
				}
			}
		}
		// no IMD, its a baba machine gun, bunker, etc.
		else if (psStructure->asWeaps[i].nStat > 0) {
			if (psStructure->getState() == STRUCTURE_STATE::BUILT) {
				const auto nWeaponStat = psStructure->weaponManager->weapons[i].stats.get();

				// get an imd to draw on the connector priority is weapon, ECM, sensor
				// check for weapon
				flashImd[i] = nWeaponStat->pMuzzleGraphic.get();

				// draw Weapon/ECM/Sensor for structure
				if (flashImd[i] != nullptr) {
					glm::mat4 matrix(1.f);
					// horrendous hack
					if (strImd->max.y > 80) // babatower
					{
						matrix *= glm::translate(glm::vec3(0.f, 80.f, 0.f)) * glm::rotate(
							UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f)) * glm::translate(
							glm::vec3(0.f, 0.f, -20.f));
					}
					else //baba bunker
					{
						matrix *= glm::translate(glm::vec3(0.f, 10.f, 0.f)) * glm::rotate(
							UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f)) * glm::translate(
							glm::vec3(0.f, 0.f, -40.f));
					}
					matrix *= glm::rotate(UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
					// draw the muzzle flash?
					if (psStructure->isVisibleToSelectedPlayer() > UBYTE_MAX / 2) {
						// animate for the duration of the flash only
						// assume no clan colours for muzzle effects
						if (flashImd[i]->numFrames == 0 || flashImd[i]->animInterval <= 0) {
							// no anim so display one frame for a fixed time
							if (graphicsTime >= psStructure->weaponManager->weapons[i].timeLastFired &&
                  graphicsTime < psStructure->weaponManager->weapons[i].timeLastFired + BASE_MUZZLE_FLASH_DURATION) {
								pie_Draw3DShape(flashImd[i], 0, colour, buildingBrightness, 0, 0,
								                modelViewMatrix * matrix); //muzzle flash
							}
						}
						else {
							const auto frame = (graphicsTime - psStructure->weaponManager->weapons[i].timeLastFired) / flashImd[i]->
								animInterval;
							if (frame < flashImd[i]->numFrames && frame >= 0) {
								pie_Draw3DShape(flashImd[i], 0, colour, buildingBrightness, 0, 0,
								                modelViewMatrix * matrix); //muzzle flash
							}
						}
					}
				}
			}
		}
		// if there is an unused connector, but not the first connector, add a light to it
		else if (psStructure->getDisplayData()->imd_shape->nconnectors > 1)
		{
			for (i = 0; i < psStructure->getDisplayData()->imd_shape->nconnectors; i++)
			{
				iIMDShape* lImd;
				lImd = getImdFromIndex(MI_LANDING);
				pie_Draw3DShape(lImd, getModularScaledGraphicsTime(lImd->animInterval, lImd->numFrames), colour,
				                buildingBrightness, 0, 0,
				                modelViewMatrix * glm::translate(
					                glm::vec3(psStructure->getDisplayData()->imd_shape->connectors->xzy())));
			}
		}
	}
}

/// Draw the structures
void renderStructure(Structure* psStructure, const glm::mat4& viewMatrix)
{
	int colour, pieFlagData, ecmFlag = 0, pieFlag = 0;
	PIELIGHT buildingBrightness;
	const Vector3i dv = Vector3i(psStructure->getPosition().x - playerPos.p.x, psStructure->getPosition().z,
	                             -(psStructure->getPosition().y - playerPos.p.z));
	bool bHitByElectronic = false;
	bool defensive = false;
	iIMDShape* strImd = psStructure->getDisplayData()->imd_shape.get();
	Tile* psTile = worldTile(psStructure->getPosition().x, psStructure->getPosition().y);
	const FACTION* faction = getPlayerFaction(psStructure->playerManager->getPlayer());

	glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) * glm::rotate(UNDEG(-psStructure->getRotation().direction),
	                                                                    glm::vec3(0.f, 1.f, 0.f));

	if (psStructure->getStats()->type == STRUCTURE_TYPE::WALL ||
      psStructure->getStats()->type == STRUCTURE_TYPE::WALL_CORNER ||
      psStructure->getStats()->type == STRUCTURE_TYPE::GATE) {
		renderWallSection(psStructure, viewMatrix);
		return;
	}
	// If the structure is not truly visible, but we know there is something there, we will instead draw a blip
	UBYTE visibilityAmount = psStructure->isVisibleToSelectedPlayer();
	if (visibilityAmount < UBYTE_MAX && visibilityAmount > 0)
	{
		int frame = graphicsTime / BLIP_ANIM_DURATION + psStructure->getId() % 8192;
		// de-sync the blip effect, but don't overflow the int
		pie_Draw3DShape(getFactionIMD(faction, getImdFromIndex(MI_BLIP)), frame, 0, WZCOL_WHITE, pie_ADDITIVE,
		                visibilityAmount / 2,
		                viewMatrix * glm::translate(glm::vec3(dv)));
		return;
	}
	else if (!visibilityAmount)
	{
		return;
	}
	else if (psStructure->getStats()->type == STRUCTURE_TYPE::DEFENSE)
	{
		defensive = true;
	}

	if (psTile->jammerBits & alliancebits[psStructure->playerManager->getPlayer()])
	{
		ecmFlag = pie_ECM;
	}

	colour = getPlayerColour(psStructure->playerManager->getPlayer());

	// -------------------------------------------------------------------------------

	/* Mark it as having been drawn */
	psStructure->setFrameNumber(currentGameFrame);

	if (!defensive
		&& psStructure->damageManager->getTimeLastHit() - graphicsTime < ELEC_DAMAGE_DURATION
		&& psStructure->damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::ELECTRONIC) {
		bHitByElectronic = true;
	}

	buildingBrightness = structureBrightness(psStructure);

	if (!defensive) {
		/* Draw the building's base first */
		if (psStructure->getStats()->base_imd != nullptr) {
			if (structureIsBlueprint(psStructure)) {
				pieFlagData = BLUEPRINT_OPACITY;
			}
			else {
				pieFlag = pie_FORCE_FOG | ecmFlag;
				pieFlagData = 255;
			}
			pie_Draw3DShape(getFactionIMD(faction, psStructure->getStats()->base_imd.get()), 0, colour,
                      buildingBrightness, pieFlag | pie_TRANSLUCENT, pieFlagData,
			                viewMatrix * modelMatrix);
		}

		// override
		if (bHitByElectronic)
		{
			buildingBrightness = pal_SetBrightness(150);
		}
	}

	if (bHitByElectronic)
	{
		modelMatrix *= objectShimmy((BaseObject *)psStructure);
	}

	const glm::mat4 viewModelMatrix = viewMatrix * modelMatrix;

	//first check if partially built - ANOTHER HACK!
	if (psStructure->getState() == STRUCTURE_STATE::BEING_BUILT) {
		if (psStructure->prebuiltImd != nullptr) {
			// strImd is a module, so render the already-built part at full height.
			pie_Draw3DShape(getFactionIMD(faction, psStructure->prebuiltImd), 0, colour, buildingBrightness, pie_SHADOW,
			                0,
			                viewModelMatrix);
		}
		pie_Draw3DShape(getFactionIMD(faction, strImd), 0, colour, buildingBrightness, pie_HEIGHT_SCALED | pie_SHADOW,
		                static_cast<int>(structHeightScale(psStructure) * pie_RAISE_SCALE), viewModelMatrix);
		setScreenDisp(psStructure->getDisplayData(), viewModelMatrix);
		return;
	}

	if (structureIsBlueprint(psStructure))
	{
		pieFlag = pie_TRANSLUCENT;
		pieFlagData = BLUEPRINT_OPACITY;
	}
	else
	{
		// structures can be rotated, so use a dynamic shadow for them
		pieFlag = pie_SHADOW | ecmFlag;
		pieFlagData = 0;
	}

	// check for animation model replacement - if none found, use animation in existing IMD
	if (strImd->objanimpie[psStructure->getAnimationEvent()])
	{
		strImd = strImd->objanimpie[psStructure->getAnimationEvent()];
	}

	while (strImd)
	{
		if (defensive && !structureIsBlueprint(psStructure) && !(strImd->flags & iV_IMD_NOSTRETCH))
		{
			pie_SetShaderStretchDepth(psStructure->getPosition().z - psStructure->getFoundationDepth());
		}
		drawShape(psStructure, getFactionIMD(faction, strImd), colour, buildingBrightness, pieFlag, pieFlagData,
		          viewModelMatrix);
		pie_SetShaderStretchDepth(0);
		if (psStructure->getDisplayData()->imd_shape->nconnectors > 0)
		{
			renderStructureTurrets(psStructure, getFactionIMD(faction, strImd), buildingBrightness, pieFlag,
			                       pieFlagData, ecmFlag, viewModelMatrix);
		}
		strImd = strImd->next;
	}
	setScreenDisp(psStructure->getDisplayData(), viewModelMatrix);
}

/// draw the delivery points
void renderDeliveryPoint(FlagPosition* psPosition, bool blueprint, const glm::mat4& viewMatrix)
{
	Vector3i dv;
	SDWORD x, y, r;
	int pieFlag, pieFlagData;
	PIELIGHT colour;

	//store the frame number for when deciding what has been clicked on
	psPosition->frameNumber = currentGameFrame;

	dv.x = psPosition->coords.x - playerPos.p.x;
	dv.z = -(psPosition->coords.y - playerPos.p.z);
	dv.y = psPosition->coords.z;

	//quick check for invalid data
	ASSERT_OR_RETURN(, psPosition->factoryType < NUM_FLAG_TYPES && psPosition->factoryInc < MAX_FACTORY_FLAG_IMDS,
	                   "Invalid assembly point");

	const glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) * glm::scale(glm::vec3(.5f)) * glm::rotate(
		-UNDEG(playerPos.r.y), glm::vec3(0, 1, 0));

	pieFlag = pie_TRANSLUCENT;
	pieFlagData = BLUEPRINT_OPACITY;

	if (blueprint) {
		colour = deliveryReposValid() ? WZCOL_LGREEN : WZCOL_LRED;
	}
	else {
		pieFlag |= pie_FORCE_FOG;
		colour = WZCOL_WHITE;
		Structure* structure = findDeliveryFactory(psPosition);
		if (structure != nullptr && structure->damageManager->isSelected())
		{
			colour = selectionBrightness();
		}
	}
	pie_Draw3DShape(pAssemblyPointIMDs[psPosition->factoryType][psPosition->factoryInc], 0, 0, colour, pieFlag,
	                pieFlagData, viewMatrix * modelMatrix);

	//get the screen coords for the DP
	calcFlagPosScreenCoords(&x, &y, &r, viewMatrix * modelMatrix);
	psPosition->screenX = x;
	psPosition->screenY = y;
	psPosition->screenR = r;
}

static bool renderWallSection(Structure* psStructure, const glm::mat4& viewMatrix)
{
	int ecmFlag = 0;
	PIELIGHT brightness;
	Vector3i dv;
	int pieFlag, pieFlagData;
	Tile* psTile = worldTile(psStructure->getPosition().x, psStructure->getPosition().y);
	const FACTION* faction = getPlayerFaction(psStructure->playerManager->getPlayer());

	if (!psStructure->isVisibleToSelectedPlayer())
	{
		return false;
	}

	if (psTile->jammerBits & alliancebits[psStructure->playerManager->getPlayer()])
	{
		ecmFlag = pie_ECM;
	}

	psStructure->setFrameNumber(currentGameFrame);

	brightness = structureBrightness(psStructure);
	pie_SetShaderStretchDepth(psStructure->getPosition().z - psStructure->getFoundationDepth());

	/* Establish where it is in the world */
	dv.x = psStructure->getPosition().x - playerPos.p.x;
	dv.z = -(psStructure->getPosition().y - playerPos.p.z);
	dv.y = psStructure->getPosition().z;

	dv.y -= gateCurrentOpenHeight(psStructure, graphicsTime, 1);
	// Make gate stick out by 1 unit, so that the tops of ┼ gates can safely have heights differing by 1 unit.

	const glm::mat4 modelMatrix = glm::translate(glm::vec3(dv)) * glm::rotate(
		UNDEG(-psStructure->getRotation().direction), glm::vec3(0.f, 1.f, 0.f));

	/* Actually render it */
	if (psStructure->getState() == STRUCTURE_STATE::BEING_BUILT) {
		pie_Draw3DShape(getFactionIMD(faction, psStructure->getDisplayData()->imd_shape.get()),
                    0, getPlayerColour(psStructure->playerManager->getPlayer()),
		                brightness, pie_HEIGHT_SCALED | pie_SHADOW | ecmFlag,
		                static_cast<int>(structHeightScale(psStructure) * pie_RAISE_SCALE),
                    viewMatrix * modelMatrix);
	}
	else
	{
		if (structureIsBlueprint(psStructure))
		{
			pieFlag = pie_TRANSLUCENT;
			pieFlagData = BLUEPRINT_OPACITY;
		}
		else
		{
			// Use a dynamic shadow
			pieFlag = pie_SHADOW;
			pieFlagData = 0;
		}
		iIMDShape* imd = psStructure->getDisplayData()->imd_shape.get();
		while (imd)
		{
			pie_Draw3DShape(getFactionIMD(faction, imd), 0,
                      getPlayerColour(psStructure->playerManager->getPlayer()), brightness,
                      pieFlag | ecmFlag, pieFlagData, viewMatrix * modelMatrix);
			imd = imd->next;
		}
	}
	setScreenDisp(psStructure->getDisplayData(), viewMatrix * modelMatrix);
	pie_SetShaderStretchDepth(0);
	return true;
}

/// SHURCOOL: Draws the strobing 3D drag box that is used for multiple selection
static void drawDragBox()
{
	if (dragBox3D.status != DRAG_DRAGGING || buildState != BUILD3D_NONE)
	{
		return;
	}

	int X1 = MIN(dragBox3D.x1, mouseX());
	int X2 = MAX(dragBox3D.x1, mouseX());
	int Y1 = MIN(dragBox3D.y1, mouseY());
	int Y2 = MAX(dragBox3D.y1, mouseY());

	// draw static box

	iV_Box(X1, Y1, X2, Y2, WZCOL_UNIT_SELECT_BORDER);
	pie_UniTransBoxFill(X1, Y1, X2, Y2, WZCOL_UNIT_SELECT_BOX);

	// draw pulse effect

	dragBox3D.pulse += (BOX_PULSE_SIZE - dragBox3D.pulse) * realTimeAdjustedIncrement(5);

	if (dragBox3D.pulse > BOX_PULSE_SIZE - 0.1f)
	{
		dragBox3D.pulse = 0;
	}

	PIELIGHT color = WZCOL_UNIT_SELECT_BOX;

	color.byte.a = static_cast<uint8_t>((float)color.byte.a * (1 - (dragBox3D.pulse / BOX_PULSE_SIZE)));
	// alpha relative to max pulse size

	pie_UniTransBoxFill(X2, Y1, X2 + dragBox3D.pulse, Y2 + dragBox3D.pulse, color); // east side + south-east corner
	pie_UniTransBoxFill(X1 - dragBox3D.pulse, Y2, X2, Y2 + dragBox3D.pulse, color); // south side + south-west corner
	pie_UniTransBoxFill(X1 - dragBox3D.pulse, Y1 - dragBox3D.pulse, X1, Y2, color); // west side + north-west corner
	pie_UniTransBoxFill(X1, Y1 - dragBox3D.pulse, X2 + dragBox3D.pulse, Y1, color); // north side + north-east corner
}


/// Display reload bars for structures and droids
static void drawWeaponReloadBar(BaseObject * psObj, Weapon* psWeap, int weapon_slot)
{
	int scrX, scrY, scrR, scale;
	float mulH; // display unit resistance instead of reload!
	int armed, firingStage;

	if (ctrlShiftDown() &&
      dynamic_cast<Droid*>(psObj)) {
		auto psDroid = dynamic_cast<Droid*>(psObj);
		scrX = psObj->getDisplayData()->screen_x;
		scrY = psObj->getDisplayData()->screen_y;
		scrR = psObj->getDisplayData()->screen_r;
		scrY += scrR + 2;

		if (weapon_slot != 0) // only rendering resistance in the first slot
		{
			return;
		}
		if (psDroid->damageManager->getResistance()) {
			mulH = (float)psDroid->damageManager->getResistance() / (float)droidResistance(psDroid);
		}
		else {
			mulH = 100.f;
		}
		firingStage = static_cast<int>(mulH);
		firingStage = ((((2 * scrR) * 10000) / 100) * firingStage) / 10000;

		if (firingStage >= (UDWORD)(2 * scrR)) {
			firingStage = (2 * scrR);
		}
		pie_BoxFill(scrX - scrR - 1, 3 + scrY + 0 + (weapon_slot * 2), scrX - scrR + (2 * scrR) + 1,
		            3 + scrY + 3 + (weapon_slot * 2), WZCOL_RELOAD_BACKGROUND);
		pie_BoxFill(scrX - scrR, 3 + scrY + 1 + (weapon_slot * 2), scrX - scrR + firingStage,
		            3 + scrY + 2 + (weapon_slot * 2), WZCOL_HEALTH_RESISTANCE);
		return;
	}

	armed = droidReloadBar(psObj, psWeap, weapon_slot);
	if (armed >= 0 && armed < 100) {
    // no need to draw if full
		scrX = psObj->getDisplayData()->screen_x;
		scrY = psObj->getDisplayData()->screen_y;
		scrR = psObj->getDisplayData()->screen_r;
    if (dynamic_cast<Droid*>(psObj)) {
      scrY += scrR + 2;
    }
    if (auto psStruct = dynamic_cast<Structure*>(psObj)) {
      scale = MAX(psStruct->getStats()->base_width,
                  psStruct->getStats()->base_breadth);
      scrY += scale * 10;
      scrR = scale * 20;
    }
		/* Scale it into an appropriate range */
		firingStage = ((((2 * scrR) * 10000) / 100) * armed) / 10000;

		if (firingStage >= 2 * scrR) {
			firingStage = (2 * scrR);
		}
		/* Power bars */
		pie_BoxFill(scrX - scrR - 1, 3 + scrY + 0 + (weapon_slot * 2), scrX - scrR + (2 * scrR) + 1,
		            3 + scrY + 3 + (weapon_slot * 2), WZCOL_RELOAD_BACKGROUND);
		pie_BoxFill(scrX - scrR, 3 + scrY + 1 + (weapon_slot * 2), scrX - scrR + firingStage,
		            3 + scrY + 2 + (weapon_slot * 2), WZCOL_RELOAD_BAR);
	}
}

/// draw target origin icon for the specified structure
static void drawStructureTargetOriginIcon(Structure* psStruct, int weapon_slot)
{
	int scrX, scrY, scrR;
	unsigned scale;

	// Process main weapon only for now
	if (!tuiTargetOrigin || weapon_slot ||
      !psStruct->weaponManager->weapons[weapon_slot].stats) {
		return;
	}

	scale = MAX(psStruct->getStats()->base_width, psStruct->getStats()->base_breadth);
	scrX = psStruct->getDisplayData()->screen_x;
	scrY = psStruct->getDisplayData()->screen_y + (scale * 10);
	scrR = scale * 20;

	/* Render target origin graphics */
	switch (psStruct->weaponManager->weapons[weapon_slot].origin) {
    using enum TARGET_ORIGIN;
	  case VISUAL:
	  	iV_DrawImage(IntImages, IMAGE_ORIGIN_VISUAL, scrX + scrR + 5, scrY - 1);
	  	break;
	  case COMMANDER:
	  	iV_DrawImage(IntImages, IMAGE_ORIGIN_COMMANDER, scrX + scrR + 5, scrY - 1);
	  	break;
	  case SENSOR:
	  	iV_DrawImage(IntImages, IMAGE_ORIGIN_SENSOR_STANDARD, scrX + scrR + 5, scrY - 1);
	  	break;
	  case CB_SENSOR:
	  	iV_DrawImage(IntImages, IMAGE_ORIGIN_SENSOR_CB, scrX + scrR + 5, scrY - 1);
	  	break;
	  case AIR_DEFENSE_SENSOR:
	  	iV_DrawImage(IntImages, IMAGE_ORIGIN_SENSOR_AIRDEF, scrX + scrR + 5, scrY - 1);
	  	break;
	  case RADAR_DETECTOR:
	  	iV_DrawImage(IntImages, IMAGE_ORIGIN_RADAR_DETECTOR, scrX + scrR + 5, scrY - 1);
	  	break;
      case UNKNOWN:
	  	break;
	  default:
	  	debug(LOG_WARNING, "Unexpected target origin in structure(%d)!", psStruct->getId());
	}
}

/// draw the health bar for the specified structure
static void drawStructureHealth(Structure* psStruct)
{
	int32_t scrX, scrY, scrR;
	PIELIGHT powerCol = WZCOL_BLACK, powerColShadow = WZCOL_BLACK;
	int32_t health, width;

	auto scale = static_cast<int32_t>(
		MAX(psStruct->getStats()->base_width, psStruct->getStats()->base_breadth));
	width = scale * 20;
	scrX = psStruct->getDisplayData()->screen_x;
	scrY = static_cast<int32_t>(psStruct->getDisplayData()->screen_y) + (scale * 10);
	scrR = width;
	//health = PERCENT(psStruct->body, psStruct->baseBodyPoints);
	if (ctrlShiftDown())
	{
		//show resistance values if CTRL/SHIFT depressed
		auto resistance = structureResistance(
			psStruct->getStats(), psStruct->playerManager->getPlayer());
		if (resistance)
		{
			health = static_cast<int32_t>(PERCENT(MAX(0, psStruct->damageManager->getResistance()), resistance));
		}
		else
		{
			health = 100;
		}
	}
	else
	{
		//show body points
		health = static_cast<int32_t>((1. - getStructureDamage(psStruct) / 65536.f) * 100);

		// If structure is incomplete, make bar correspondingly thinner.
		int maxBody = structureBody(psStruct);
		int maxBodyBuilt = structureBodyBuilt(psStruct);
		width = (uint64_t)width * maxBodyBuilt / maxBody;
	}
	if (health > REPAIRLEV_HIGH)
	{
		powerCol = WZCOL_HEALTH_HIGH;
		powerColShadow = WZCOL_HEALTH_HIGH_SHADOW;
	}
	else if (health > REPAIRLEV_LOW)
	{
		powerCol = WZCOL_HEALTH_MEDIUM;
		powerColShadow = WZCOL_HEALTH_MEDIUM_SHADOW;
	}
	else
	{
		powerCol = WZCOL_HEALTH_LOW;
		powerColShadow = WZCOL_HEALTH_LOW_SHADOW;
	}
	health = (((width * 10000) / 100) * health) / 10000;
	health *= 2;
	pie_BoxFillf(scrX - scrR - 1, scrY - 1, scrX - scrR + 2 * width + 1, scrY + 3, WZCOL_RELOAD_BACKGROUND);
	pie_BoxFillf(scrX - scrR, scrY, scrX - scrR + health, scrY + 1, powerCol);
	pie_BoxFillf(scrX - scrR, scrY + 1, scrX - scrR + health, scrY + 2, powerColShadow);
}

/// draw the construction bar for the specified structure
static void drawStructureBuildProgress(Structure* psStruct)
{
	auto scale = static_cast<int32_t>(
		MAX(psStruct->getStats()->base_width, psStruct->getStats()->base_breadth));
	auto scrX = static_cast<int32_t>(psStruct->getDisplayData()->screen_x);
	int32_t scrY = static_cast<int32_t>(psStruct->getDisplayData()->screen_y) + (scale * 10);
	int32_t scrR = scale * 20;
	auto progress = scale * 40 * structureCompletionProgress(*psStruct);
	pie_BoxFillf(scrX - scrR - 1, scrY - 1 + 5, scrX + scrR + 1, scrY + 3 + 5, WZCOL_RELOAD_BACKGROUND);
	pie_BoxFillf(scrX - scrR, scrY + 5, scrX - scrR + progress, scrY + 1 + 5, WZCOL_HEALTH_MEDIUM_SHADOW);
	pie_BoxFillf(scrX - scrR, scrY + 1 + 5, scrX - scrR + progress, scrY + 2 + 5, WZCOL_HEALTH_MEDIUM);
}

/// Draw the health of structures and show enemy structures being targeted
static void drawStructureSelections()
{
	Structure* psStruct;
	SDWORD scrX, scrY;
	UDWORD i;
  BaseObject * psClickedOn;
	bool bMouseOverStructure = false;
	bool bMouseOverOwnStructure = false;

	psClickedOn = mouseTarget();
	if (psClickedOn != nullptr && dynamic_cast<Structure*>(psClickedOn))
	{
		bMouseOverStructure = true;
		if (psClickedOn->playerManager->getPlayer() == selectedPlayer)
		{
			bMouseOverOwnStructure = true;
		}
	}
	pie_SetFogStatus(false);

	if (selectedPlayer >= MAX_PLAYERS) { return; /* no-op */ }

	/* Go thru' all the buildings */
	for (auto& psStruct : playerList[selectedPlayer].structures)
	{
		if (psStruct.getDisplayData()->frame_number == currentGameFrame) {
			/* If it's selected */
			if (psStruct.damageManager->isSelected() ||
			  	(barMode == BAR_DROIDS_AND_STRUCTURES &&
           psStruct.getStats()->type != STRUCTURE_TYPE::WALL &&
           psStruct.getStats()->type != STRUCTURE_TYPE::WALL_CORNER) ||
				  (bMouseOverOwnStructure &&
           &psStruct == dynamic_cast<Structure*>(psClickedOn))) {
				drawStructureHealth(&psStruct);

				for (i = 0; i < numWeapons(psStruct); i++)
				{
					drawWeaponReloadBar(&psStruct, &psStruct.weaponManager->weapons[i], i);
					drawStructureTargetOriginIcon(&psStruct, i);
				}
			}

			if (psStruct.getState() == STRUCTURE_STATE::BEING_BUILT) {
				drawStructureBuildProgress(&psStruct);
			}
		}
	}

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		for (auto& psStruct : playerList[i].structures)
		{
			/* If it's targetted and on-screen */
			if (psStruct.testFlag((size_t)OBJECT_FLAG::TARGETED)
				&& psStruct.getDisplayData()->frame_number == currentGameFrame) {
				scrX = psStruct.getDisplayData()->screen_x;
				scrY = psStruct.getDisplayData()->screen_y;
				iV_DrawImage(IntImages, getTargettingGfx(), scrX, scrY);
			}
		}
	}

	if (bMouseOverStructure && !bMouseOverOwnStructure)
	{
		if (mouseDown(getRightClickOrders() ? MOUSE_LMB : MOUSE_RMB))
		{
			psStruct = dynamic_cast<Structure*>(psClickedOn);
			drawStructureHealth(psStruct);
			if (psStruct->getState() == STRUCTURE_STATE::BEING_BUILT)
			{
				drawStructureBuildProgress(psStruct);
			}
		}
	}
}

static UDWORD getTargettingGfx()
{
	const unsigned index = getModularScaledRealTime(1000, 10);
	switch (index)
	{
	case 0:
	case 1:
	case 2:
		return (IMAGE_TARGET1 + index);
		break;
	default:
		if (index & 0x01)
		{
			return (IMAGE_TARGET4);
		}
		else
		{
			return (IMAGE_TARGET5);
		}
		break;
	}
}

/// Is the droid, its commander or its sensor tower selected?
bool eitherSelected(Droid const* psDroid)
{
	bool retVal = false;
	if (psDroid->damageManager->isSelected()) {
		retVal = true;
	}

	if (psDroid->getGroup()) {
		if (psDroid->getCommander()) {
			if (psDroid->getCommander()->damageManager->isSelected()) {
				retVal = true;
			}
		}
	}

  auto const psObj = orderStateObj(psDroid, ORDER_TYPE::FIRE_SUPPORT);
	if (psObj && psObj->damageManager->isSelected()) {
		retVal = true;
	}
	return retVal;
}

void drawDroidSelection(Droid* psDroid, bool drawBox)
{
	if (psDroid->getDisplayData()->frame_number != currentGameFrame)
	{
		return; // Not visible, anyway. Don't bother with health bars.
	}

	UDWORD damage = PERCENT(psDroid->damageManager->getHp(), psDroid->damageManager->getOriginalHp());

	PIELIGHT powerCol = WZCOL_BLACK;
	PIELIGHT powerColShadow = WZCOL_BLACK;

	if (damage > REPAIRLEV_HIGH)
	{
		powerCol = WZCOL_HEALTH_HIGH;
		powerColShadow = WZCOL_HEALTH_HIGH_SHADOW;
	}
	else if (damage > REPAIRLEV_LOW)
	{
		powerCol = WZCOL_HEALTH_MEDIUM;
		powerColShadow = WZCOL_HEALTH_MEDIUM_SHADOW;
	}
	else
	{
		powerCol = WZCOL_HEALTH_LOW;
		powerColShadow = WZCOL_HEALTH_LOW_SHADOW;
	}

	damage = static_cast<UDWORD>((float)psDroid->damageManager->getHp() /
          (float)psDroid->damageManager->getOriginalHp() * (float)psDroid->getDisplayData()->screen_r);

	if (damage > psDroid->getDisplayData()->screen_r)
	{
		damage = psDroid->getDisplayData()->screen_r;
	}

	damage *= 2;

	std::vector<PIERECT_DrawRequest> rectsToDraw;
	if (drawBox)
	{
		rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r - 7,
                                              psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r + 1,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r, WZCOL_WHITE);
		rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r,
                                              psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r + 7,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 1,
		                                          WZCOL_WHITE);
		rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r - 7,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r,
                                              psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 1,
		                                          WZCOL_WHITE);
		rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r - 7,
                                              psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r + 1,
                                              psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 1,
		                                          WZCOL_WHITE);
	}

	/* Power bars */
	rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r - 1,
                                            psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 2,
                                            psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r + 1,
                                            psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 6,
	                                          WZCOL_RELOAD_BACKGROUND);
	rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r,
                                            psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 3,
                                            psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r + damage,
                                            psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 4, powerCol);
	rectsToDraw.emplace_back(psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r,
                                            psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 4,
                                            psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r + damage,
                                            psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r + 5,
	                                          powerColShadow);

	pie_DrawMultiRect(rectsToDraw);


	/* Write the droid rank out */
	if ((psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r) > 0
	  	&& (psDroid->getDisplayData()->screen_x - psDroid->getDisplayData()->screen_r) < pie_GetVideoBufferWidth()
		  && (psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r) > 0
		  && (psDroid->getDisplayData()->screen_y - psDroid->getDisplayData()->screen_r) < pie_GetVideoBufferHeight()) {
		drawDroidRank(psDroid);
		drawDroidSensorLock(psDroid);
		drawDroidCmndNo(psDroid);
		drawDroidGroupNumber(psDroid);
	}

	for (auto i = 0; i < numWeapons(*psDroid); i++)
	{
		drawWeaponReloadBar(psDroid, &psDroid->weaponManager->weapons[i], i);
	}
}

/// Draw the selection graphics for selected droids
static void drawDroidSelections()
{
	PIELIGHT powerCol = WZCOL_BLACK, powerColShadow = WZCOL_BLACK;
	PIELIGHT boxCol;
  BaseObject * psClickedOn;
	bool bMouseOverDroid = false;
	bool bMouseOverOwnDroid = false;
	unsigned index;
	float mulH;

	psClickedOn = mouseTarget();
	if (psClickedOn != nullptr &&
      dynamic_cast<Droid*>(psClickedOn)) {
		bMouseOverDroid = true;
		if (psClickedOn->playerManager->isSelectedPlayer() &&
        !psClickedOn->damageManager->isSelected()) {
			bMouseOverOwnDroid = true;
		}
	}

	if (selectedPlayer >= MAX_PLAYERS) { return; /* no-op */ }

	pie_SetFogStatus(false);
	for (auto& psDroid : playerList[selectedPlayer].droids)
	{
		/* If it's selected and on screen or it's the one the mouse is over */
		if (eitherSelected(&psDroid) ||
        (bMouseOverOwnDroid && psDroid == (Droid*)psClickedOn) ||
        droidUnderRepair(&psDroid) ||
			barMode == BAR_DROIDS || barMode == BAR_DROIDS_AND_STRUCTURES
		) {
			drawDroidSelection(&psDroid, psDroid.damageManager->isSelected());
		}
	}

	/* Are we over an enemy droid */
	if (bMouseOverDroid && !bMouseOverOwnDroid) {
		if (mouseDown(getRightClickOrders() ? MOUSE_LMB : MOUSE_RMB)) {
			if (!psClickedOn->playerManager->isSelectedPlayer() &&
          psClickedOn->getDisplayData()->frame_number == currentGameFrame) {
				Droid* psDroid = (Droid*)psClickedOn;
				UDWORD damage;
				//show resistance values if CTRL/SHIFT depressed
				if (ctrlShiftDown()) {
					if (psDroid->damageManager->getResistance()) {
						damage = PERCENT(psDroid->damageManager->getResistance(), droidResistance(psDroid));
					}
					else {
						damage = 100;
					}
				}
				else {
					damage = PERCENT(psDroid->damageManager->getHp(), psDroid->damageManager->getOriginalHp());
				}

				if (damage > REPAIRLEV_HIGH)
				{
					powerCol = WZCOL_HEALTH_HIGH;
					powerColShadow = WZCOL_HEALTH_HIGH_SHADOW;
				}
				else if (damage > REPAIRLEV_LOW)
				{
					powerCol = WZCOL_HEALTH_MEDIUM;
					powerColShadow = WZCOL_HEALTH_MEDIUM_SHADOW;
				}
				else
				{
					powerCol = WZCOL_HEALTH_LOW;
					powerColShadow = WZCOL_HEALTH_LOW_SHADOW;
				}

				//show resistance values if CTRL/SHIFT depressed
				if (ctrlShiftDown())
				{
					if (psDroid->damageManager->getResistance()) {
						mulH = (float)psDroid->damageManager->getResistance() / (float)droidResistance(psDroid);
					}
					else
					{
						mulH = 100.f;
					}
				}
				else
				{
					mulH = (float)psDroid->damageManager->getHp() / (float)psDroid->damageManager->getOriginalHp();
				}
				damage = static_cast<UDWORD>(mulH * (float)psDroid->getDisplayData()->screen_r);
				// (((psDroid->sDisplay.screenR*10000)/100)*damage)/10000;
				if (damage > psDroid->getDisplayData()->screen_r)
				{
					damage = psDroid->getDisplayData()->screen_r;
				}
				damage *= 2;
				auto scrX = psDroid->getDisplayData()->screen_x;
				auto scrY = psDroid->getDisplayData()->screen_y;
				auto scrR = psDroid->getDisplayData()->screen_r;

				/* Three DFX clips properly right now - not sure if software does */
				if ((scrX + scrR) > 0
					&& (scrX - scrR) < pie_GetVideoBufferWidth()
					&& (scrY + scrR) > 0
					&& (scrY - scrR) < pie_GetVideoBufferHeight())
				{
					boxCol = WZCOL_WHITE;

					/* Power bars */
					pie_BoxFill(scrX - scrR - 1, scrY + scrR + 2, scrX + scrR + 1, scrY + scrR + 6,
					            WZCOL_RELOAD_BACKGROUND);
					pie_BoxFill(scrX - scrR, scrY + scrR + 3, scrX - scrR + damage, scrY + scrR + 4, powerCol);
					pie_BoxFill(scrX - scrR, scrY + scrR + 4, scrX - scrR + damage, scrY + scrR + 5, powerColShadow);
				}
			}
		}
	}

	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		/* Go thru' all the droidss */
		for (auto const& psDroid : playerList[i].droids)
		{
			if (showORDERS) {
				drawDroidOrder(&psDroid);
			}
			if (!psDroid.damageManager->isDead() && psDroid.getDisplayData()->frame_number == currentGameFrame) {
				/* If it's selected */
				if (psDroid.testFlag((size_t)OBJECT_FLAG::TARGETED) &&
            psDroid.isVisibleToSelectedPlayer() == UBYTE_MAX) {
					index = IMAGE_BLUE1 + getModularScaledRealTime(1020, 5);
					iV_DrawImage(IntImages, index, psDroid.getDisplayData()->screen_x, psDroid.getDisplayData()->screen_y);
				}
			}
		}
	}

	for (const auto psFeature : apsFeatureLists[0])
	{
		if (!psFeature->damageManager->isDead() && psFeature->getDisplayData()->frame_number == currentGameFrame) {
			if (psFeature->testFlag((size_t)OBJECT_FLAG::TARGETED)) {
				iV_DrawImage(IntImages, getTargettingGfx(),
                     psFeature->getDisplayData()->screen_x,
                     psFeature->getDisplayData()->screen_y);
			}
		}
	}
}

/// X offset to display the group number at
static constexpr auto GN_X_OFFSET = 8;

/// Draw the number of the group the droid is in next to the droid
static void drawDroidGroupNumber(Droid const* psDroid)
{
	uint16_t id = UWORD_MAX;

	switch (psDroid->getSelectionGroup()) {
	  case 0:
	  	id = IMAGE_GN_0;
	  	break;
	  case 1:
	  	id = IMAGE_GN_1;
	  	break;
	  case 2:
	  	id = IMAGE_GN_2;
	  	break;
	  case 3:
	  	id = IMAGE_GN_3;
	  	break;
	  case 4:
	  	id = IMAGE_GN_4;
	  	break;
	  case 5:
	  	id = IMAGE_GN_5;
	  	break;
	  case 6:
	  	id = IMAGE_GN_6;
	  	break;
	  case 7:
	  	id = IMAGE_GN_7;
	  	break;
	  case 8:
	  	id = IMAGE_GN_8;
	  	break;
	  case 9:
	  	id = IMAGE_GN_9;
	  	break;
	  default:
	  	break;
	}

	if (id != UWORD_MAX) {
		int xShift = psDroid->getDisplayData()->screen_r + GN_X_OFFSET;
		int yShift = psDroid->getDisplayData()->screen_r;
		iV_DrawImage(IntImages, id, psDroid->getDisplayData()->screen_x - xShift, psDroid->getDisplayData()->screen_y + yShift);
	}
}

/// X offset to display the group number at
#define CMND_STAR_X_OFFSET	(6)
#define CMND_GN_Y_OFFSET	(8)

static void drawDroidOrder(const Droid* psDroid)
{
	const int xShift = psDroid->getDisplayData()->screen_r + GN_X_OFFSET;
	const int yShift = psDroid->getDisplayData()->screen_r - CMND_GN_Y_OFFSET;
	auto letter = getDroidOrderKey(psDroid->getOrder()->type);
	iV_SetTextColour(WZCOL_TEXT_BRIGHT);
	iV_DrawText(letter.c_str(), psDroid->getDisplayData()->screen_x - xShift - CMND_STAR_X_OFFSET, psDroid->getDisplayData()->screen_y + yShift,
              font_regular);
}

/// Draw the number of the commander the droid is assigned to
static void drawDroidCmndNo(Droid* psDroid)
{
	SDWORD xShift, yShift, index;
	UDWORD id2;
	UWORD id;
	bool bDraw = true;

	id = UWORD_MAX;

	id2 = IMAGE_GN_STAR;
	index = SDWORD_MAX;
	if (psDroid->getType() == DROID_TYPE::COMMAND) {
		index = cmdDroidGetIndex(psDroid);
	}
	else if (hasCommander(psDroid)) {
		index = cmdDroidGetIndex(psDroid->getCommander());
	}
	switch (index)
	{
	case 1:
		id = IMAGE_GN_1;
		break;
	case 2:
		id = IMAGE_GN_2;
		break;
	case 3:
		id = IMAGE_GN_3;
		break;
	case 4:
		id = IMAGE_GN_4;
		break;
	case 5:
		id = IMAGE_GN_5;
		break;
	case 6:
		id = IMAGE_GN_6;
		break;
	case 7:
		id = IMAGE_GN_7;
		break;
	case 8:
		id = IMAGE_GN_8;
		break;
	case 9:
		id = IMAGE_GN_9;
		break;
	default:
		bDraw = false;
		break;
	}

	if (bDraw)
	{
		xShift = psDroid->getDisplayData()->screen_r + GN_X_OFFSET;
		yShift = psDroid->getDisplayData()->screen_r - CMND_GN_Y_OFFSET;
		iV_DrawImage(IntImages, id2, psDroid->getDisplayData()->screen_x - xShift - CMND_STAR_X_OFFSET,
                 psDroid->getDisplayData()->screen_y + yShift);
		iV_DrawImage(IntImages, id, psDroid->getDisplayData()->screen_x - xShift, psDroid->getDisplayData()->screen_y + yShift);
	}
}

/* ---------------------------------------------------------------------------- */


/**	Get the onscreen coordinates of a droid so we can draw a bounding box
 * This need to be severely speeded up and the accuracy increased to allow variable size bouding boxes
 * @todo Remove all magic numbers and hacks
 */
void calcScreenCoords(Droid* psDroid, const glm::mat4& viewMatrix)
{
	/* Get it's absolute dimensions */
	auto psBStats =psDroid->getComponent(COMPONENT_TYPE::BODY);
	Vector3i origin;
	Vector2i center(0, 0);
	int wsRadius = 22; // World space radius, 22 = magic minimum
	float radius;

	// NOTE: This only takes into account body, but seems "good enough"
	if (psBStats && psBStats->pIMD) {
		wsRadius = MAX(wsRadius, psBStats->pIMD->radius);
	}

	origin = Vector3i(0, wsRadius, 0); // take the center of the object

	/* get the screen coordinates */
	const float cZ = pie_RotateProject(&origin, viewMatrix, &center) * 0.1f;

	// avoid division by zero
	if (cZ > 0) {
		radius = wsRadius / cZ * pie_GetResScalingFactor();
	}
	else {
		radius = 1; // 1 just in case some other code assumes radius != 0
	}

	/* Deselect all the droids if we've released the drag box */
	if (dragBox3D.status == DRAG_RELEASED) {
		if (inQuad(&center, &dragQuad) && psDroid->playerManager->isSelectedPlayer()) {
			//don't allow Transporter Droids to be selected here
			//unless we're in multiPlayer mode!!!!
			if (!isTransporter(*psDroid) || bMultiPlayer) {
				dealWithDroidSelect(psDroid, true);
			}
		}
	}

	/* Store away the screen coordinates so we can select the droids without doing a transform */
	psDroid->sDisplay.screen_x = center.x;
	psDroid->sDisplay.screen_y = center.y;
	psDroid->sDisplay.screen_r = static_cast<UDWORD>(radius);
}

void screenCoordToWorld(Vector2i screenCoord, Vector2i& worldCoord, SDWORD& tileX, SDWORD& tileY)
{
	int nearestZ = INT_MAX;
	Vector2i outMousePos(0, 0);
	// Intentionally not the same range as in drawTiles()
	for (int i = -visibleTiles.y / 2, idx = 0; i < visibleTiles.y / 2; i++, ++idx)
	{
		for (int j = -visibleTiles.x / 2, jdx = 0; j < visibleTiles.x / 2; j++, ++jdx)
		{
			const int tileZ = tileScreenInfo[idx][jdx].z;

			if (tileZ <= nearestZ)
			{
				QUAD quad;

				quad.coords[0].x = tileScreenInfo[idx + 0][jdx + 0].x;
				quad.coords[0].y = tileScreenInfo[idx + 0][jdx + 0].y;

				quad.coords[1].x = tileScreenInfo[idx + 0][jdx + 1].x;
				quad.coords[1].y = tileScreenInfo[idx + 0][jdx + 1].y;

				quad.coords[2].x = tileScreenInfo[idx + 1][jdx + 1].x;
				quad.coords[2].y = tileScreenInfo[idx + 1][jdx + 1].y;

				quad.coords[3].x = tileScreenInfo[idx + 1][jdx + 0].x;
				quad.coords[3].y = tileScreenInfo[idx + 1][jdx + 0].y;

				/* We've got a match for our mouse coords */
				if (inQuad(&screenCoord, &quad))
				{
					outMousePos.x = playerPos.p.x + world_coord(j);
					outMousePos.y = playerPos.p.z + world_coord(i);
					outMousePos += positionInQuad(screenCoord, quad);
					if (outMousePos.x < 0)
					{
						outMousePos.x = 0;
					}
					else if (outMousePos.x > world_coord(mapWidth - 1))
					{
						outMousePos.x = world_coord(mapWidth - 1);
					}
					if (outMousePos.y < 0)
					{
						outMousePos.y = 0;
					}
					else if (outMousePos.y > world_coord(mapHeight - 1))
					{
						outMousePos.y = world_coord(mapHeight - 1);
					}
					tileX = map_coord(outMousePos.x);
					tileY = map_coord(outMousePos.y);
					/* Store away z value */
					nearestZ = tileZ;
				}
			}
		}
	}
	worldCoord = outMousePos;
}

/**
 * Find the tile the mouse is currently over
 * \todo This is slow - speed it up
 */
static void locateMouse()
{
	const Vector2i pt(mouseX(), mouseY());
	screenCoordToWorld(pt, mousePos, mouseTileX, mouseTileY);
}

/// Render the sky and surroundings
static void renderSurroundings(const glm::mat4& viewMatrix)
{
	// Render skybox relative to ground (i.e. undo player y translation)
	// then move it somewhat below ground level for the blending effect
	// rotate it

	if (!gamePaused())
	{
		wind = std::remainder(wind + graphicsTimeAdjustedIncrement(windSpeed), 360.0f);
	}

	// TODO: skybox needs to be just below lowest point on map (because we have a bottom cap now). Hardcoding for now.
	pie_DrawSkybox(skybox_scale,
	               viewMatrix * glm::translate(glm::vec3(0.f, -500, 0.f)) * glm::rotate(
		               RADIANS(wind), glm::vec3(0.f, 1.f, 0.f)));
}

static int calculateCameraHeight(int _mapHeight)
{
	return static_cast<int>(std::ceil(static_cast<float>(_mapHeight) / static_cast<float>(HEIGHT_TRACK_INCREMENTS))) *
		HEIGHT_TRACK_INCREMENTS + CAMERA_PIVOT_HEIGHT;
}

int calculateCameraHeightAt(int tileX, int tileY)
{
	return calculateCameraHeight(calcAverageTerrainHeight(tileX, tileY));
}

/// Smoothly adjust player height to match the desired height
static void trackHeight(int desiredHeight)
{
	static const uint32_t minTrackHeightInterval = GAME_TICKS_PER_SEC / 60;
	static uint32_t lastHeightAdjustmentRealTime = 0;
	static float heightSpeed = 0.0f;

	uint32_t deltaTrackHeightRealTime = realTime - lastHeightAdjustmentRealTime;
	if (deltaTrackHeightRealTime < minTrackHeightInterval)
	{
		// avoid processing this too rapidly, such as when vsync is disabled
		return;
	}
	lastHeightAdjustmentRealTime = realTime;

	if ((desiredHeight == playerPos.p.y) && ((heightSpeed > -5.f) && (heightSpeed < 5.f)))
	{
		heightSpeed = 0.0f;
		return;
	}

	float separation = desiredHeight - playerPos.p.y; // How far are we from desired height?

	// d²/dt² player.p.y = -ACCEL_CONSTANT * (player.p.y - desiredHeight) - VELOCITY_CONSTANT * d/dt player.p.y
	solveDifferential2ndOrder(&separation, &heightSpeed, ACCEL_CONSTANT, VELOCITY_CONSTANT,
	                          (float)deltaTrackHeightRealTime / (float)GAME_TICKS_PER_SEC);

	/* Adjust the height accordingly */
	playerPos.p.y = desiredHeight - static_cast<int>(std::trunc(separation));
}

/// Select the next energy bar display mode
ENERGY_BAR toggleEnergyBars()
{
	if (++barMode == BAR_LAST)
	{
		barMode = BAR_SELECTED;
	}
	return (ENERGY_BAR)barMode;
}

/// Set everything up for when the player assigns the sensor target
void assignSensorTarget(BaseObject * psObj)
{
	bSensorTargetting = true;
	lastTargetAssignation = realTime;
	psSensorObj = psObj;
}

/// Set everything up for when the player selects the destination
void assignDestTarget()
{
	bDestTargetting = true;
	lastDestAssignation = realTime;
	destTargetX = mouseX();
	destTargetY = mouseY();
	destTileX = mouseTileX;
	destTileY = mouseTileY;
}

/// Draw a graphical effect after selecting a sensor target
static void processSensorTarget()
{
  if (!bSensorTargetting)
    return;

  if ((realTime - lastTargetAssignation) < TARGET_TO_SENSOR_TIME)
  {
    if (!psSensorObj->damageManager->isDead() && psSensorObj->getDisplayData()->frame_number == currentGameFrame)
    {
      const int x = /*mouseX();*/(SWORD)psSensorObj->getDisplayData()->screen_x;
      const int y = (SWORD)psSensorObj->getDisplayData()->screen_y;
      int index = IMAGE_BLUE1;
      if (!gamePaused())
      {
        index = IMAGE_BLUE1 + getModularScaledGraphicsTime(1020, 5);
      }
      iV_DrawImage(IntImages, index, x, y);
      const int offset = 12 + ((TARGET_TO_SENSOR_TIME) - (realTime - lastTargetAssignation)) / 2;
      const int x0 = x - offset;
      const int y0 = y - offset;
      const int x1 = x + offset;
      const int y1 = y + offset;
      const std::vector<glm::ivec4> lines = {
        glm::ivec4(x0, y0, x0 + 8, y0), glm::ivec4(x0, y0, x0, y0 + 8),
        glm::ivec4(x1, y0, x1 - 8, y0), glm::ivec4(x1, y0, x1, y0 + 8),
        glm::ivec4(x1, y1, x1 - 8, y1), glm::ivec4(x1, y1, x1, y1 - 8),
        glm::ivec4(x0, y1, x0 + 8, y1), glm::ivec4(x0, y1, x0, y1 - 8),
        glm::ivec4(x0, y0, x0 + 8, y0), glm::ivec4(x0, y0, x0, y0 + 8)
      };
      iV_Lines(lines, WZCOL_WHITE);
    }
    else
    {
      bSensorTargetting = false;
    }
  }
  else
  {
    bSensorTargetting = false;
  }
}

/// Draw a graphical effect after selecting a destination
static void processDestinationTarget()
{
  if (!bDestTargetting)
    return;

  if ((realTime - lastDestAssignation) < DEST_TARGET_TIME)
  {
    const int x = destTargetX;
    const int y = destTargetY;
    const int offset = ((DEST_TARGET_TIME) - (realTime - lastDestAssignation)) / 2;
    const int x0 = x - offset;
    const int y0 = y - offset;
    const int x1 = x + offset;
    const int y1 = y + offset;

    pie_BoxFill(x0, y0, x0 + 2, y0 + 2, WZCOL_WHITE);
    pie_BoxFill(x1 - 2, y0 - 2, x1, y0, WZCOL_WHITE);
    pie_BoxFill(x1 - 2, y1 - 2, x1, y1, WZCOL_WHITE);
    pie_BoxFill(x0, y1, x0 + 2, y1 + 2, WZCOL_WHITE);
  }
  else
  {
    bDestTargetting = false;
  }
}

/// Set what tile is being used to draw the bottom of a body of water
void setUnderwaterTile(UDWORD num)
{
	underwaterTile = num;
}

/// Set what tile is being used to show rubble
void setRubbleTile(UDWORD num)
{
	rubbleTile = num;
}

/// Get the tile that is currently being used to draw underwater ground
UDWORD getWaterTileNum()
{
	return (underwaterTile);
}

/// Get the tile that is being used to show rubble
UDWORD getRubbleTileNum()
{
	return (rubbleTile);
}

/// Draw the spinning particles for power stations and re-arm pads for the specified player
static void structureEffectsPlayer(UDWORD player)
{
	SDWORD radius;
	SDWORD xDif, yDif;
	Vector3i pos;
	UDWORD gameDiv;

	const int effectsPerSecond = 12;
	// Effects per second. Will add effects up to once time per frame, so won't add as many effects if the framerate is low, but will be consistent, otherwise.
	unsigned effectTime = graphicsTime / (GAME_TICKS_PER_SEC / effectsPerSecond) * (GAME_TICKS_PER_SEC /
		effectsPerSecond);
	if (effectTime <= graphicsTime - deltaGraphicsTime)
	{
		return; // Don't add effects this frame.
	}

	for (auto& psStructure : playerList[player].structures)
	{
		if (psStructure.getState() != STRUCTURE_STATE::BUILT) {
			continue;
		}
		if (psStructure.getStats()->type == STRUCTURE_TYPE::POWER_GEN &&
        psStructure.isVisibleToSelectedPlayer()) {
			PowerGenerator* psPowerGen = &psStructure.pFunctionality->powerGenerator;
			unsigned numConnected = 0;
			for (auto i = 0; i < NUM_POWER_MODULES; i++)
			{
				if (psPowerGen->getExtractor(i)) {
					numConnected++;
				}
			}
			/* No effect if nothing connected */
			if (!numConnected) {
				//keep looking for another!
				continue;
			}
			else
				switch (numConnected) {
				case 1:
				case 2:
					gameDiv = 1440;
					break;
				case 3:
				case 4:
				default:
					gameDiv = 1080; // really fast!!!
					break;
				}

			/* New addition - it shows how many are connected... */
			for (auto i = 0; i < numConnected; i++)
			{
				radius = 32 - (i * 2); // around the spire
				xDif = iSinSR(effectTime, gameDiv, radius);
				yDif = iCosSR(effectTime, gameDiv, radius);

				pos.x = psStructure.getPosition().x + xDif;
				pos.z = psStructure.getPosition().y + yDif;
				pos.y = map_Height(pos.x, pos.z) + 64 + (i * 20); // 64 up to get to base of spire
				effectGiveAuxVar(50); // half normal plasma size...
				addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_LASER, false, nullptr, 0);

				pos.x = psStructure.getPosition().x - xDif;
				pos.z = psStructure.getPosition().y - yDif;
				effectGiveAuxVar(50); // half normal plasma size...

				addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_LASER, false, nullptr, 0);
			}
		}
		/* Might be a re-arm pad! */
		else if (psStructure.getStats()->type == STRUCTURE_TYPE::REARM_PAD
			&& psStructure.isVisibleToSelectedPlayer())
		{
			RearmPad* psReArmPad = &psStructure.pFunctionality->rearmPad;
      BaseObject * psChosenObj = psReArmPad->psObj;
			if (psChosenObj != nullptr && (((Droid*)psChosenObj)->isVisibleToSelectedPlayer()))
			{
				unsigned bFXSize = 0;
				Droid* psDroid = (Droid*)psChosenObj;
				if (!psDroid->damageManager->isDead() && psDroid->getAction() == ACTION::WAIT_DURING_REARM)
				{
					bFXSize = 30;
				}
				/* Then it's repairing...? */
				radius = psStructure.getDisplayData()->imd_shape->radius;
				xDif = iSinSR(effectTime, 720, radius);
				yDif = iCosSR(effectTime, 720, radius);
				pos.x = psStructure.getPosition().x + xDif;
				pos.z = psStructure.getPosition().y + yDif;
				pos.y = map_Height(pos.x, pos.z) + psStructure.getDisplayData()->imd_shape->max.y;
				effectGiveAuxVar(30 + bFXSize); // half normal plasma size...
				addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_LASER, false, nullptr, 0);
				pos.x = psStructure.getPosition().x - xDif;
				pos.z = psStructure.getPosition().y - yDif; // buildings are level!
				effectGiveAuxVar(30 + bFXSize); // half normal plasma size...
				addEffect(&pos, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_LASER, false, nullptr, 0);
			}
		}
	}
}

/// Draw the effects for all players and buildings
static void structureEffects()
{
	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		if (!playerList[i].structures.empty()) {
			structureEffectsPlayer(i);
		}
	}
}

/// Show the sensor ranges of selected droids and buildings
static void showDroidSensorRanges()
{
	if (selectedPlayer >= MAX_PLAYERS) {
    return; /* no-op */
  }

	if (rangeOnScreen)
	// note, we still have to decide what to do with multiple units selected, since it will draw it for all of them! -Q 5-10-05
	{
		for (auto& psDroid : playerList[selectedPlayer].droids)
		{
			if (psDroid.damageManager->isSelected()) {
				showSensorRange2(&psDroid);
			}
		}

		for (auto& psStruct : playerList[selectedPlayer].structures)
		{
			if (psStruct.damageManager->isSelected()) {
				showSensorRange2(&psStruct);
			}
		}
	} //end if we want to display...
}

static void showEffectCircle(Position centre, int radius, unsigned auxVar,
                             EFFECT_GROUP group, EFFECT_TYPE type)
{
  // 2πr in tiles.
	const auto circumference = radius * 2 * 355 / 113 / TILE_UNITS;
	for (int i = 0; i < circumference; ++i)
	{
		Vector3i pos;
		pos.x = centre.x - iSinSR(i, circumference, radius);
		pos.z = centre.y - iCosSR(i, circumference, radius); // [sic] y -> z

		// check if it's actually on map
		if (worldOnMap(pos.x, pos.z)) {
			pos.y = map_Height(pos.x, pos.z) + 16;
			effectGiveAuxVar(auxVar);
			addEffect(&pos, group, type, false, nullptr, 0);
		}
	}
}

// Shows the weapon (long) range of the object in question.
// Note, it only does it for the first weapon slot!
static void showWeaponRange(BaseObject const* psObj)
{
	WeaponStats const* psStats;

	if (auto psDroid = dynamic_cast<Droid const*>(psObj)) {
		psStats = psDroid->weaponManager->weapons[0].stats.get();
	}
	else {
		auto psStruct = dynamic_cast<Structure const*>(psObj);
		if (numWeapons(*psStruct) == 0) {
			return;
		}
		psStats = psStruct->getStats()->psWeapStat[0];
	}
	const auto weaponRange = proj_GetLongRange(psStats, psObj->playerManager->getPlayer());
	const auto minRange = proj_GetMinRange(psStats, psObj->playerManager->getPlayer());
	showEffectCircle(psObj->getPosition(), weaponRange, 40,
                   EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_SMALL);
  
	if (minRange > 0) {
		showEffectCircle(psObj->getPosition(), minRange, 40,
                     EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_TESLA);
	}
}

static void showSensorRange2(BaseObject * psObj)
{
	showEffectCircle(psObj->getPosition(), objSensorRange(psObj), 80,
                   EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_LASER);
  
	showWeaponRange(psObj);
}

/// Draw a circle on the map (to show the range of something)
static void drawRangeAtPos(SDWORD centerX, SDWORD centerY, SDWORD radius)
{
	Position pos(centerX, centerY, 0); // .z ignored.
	showEffectCircle(pos, radius, 80, EFFECT_GROUP::EXPLOSION, EFFECT_TYPE::EXPLOSION_TYPE_SMALL);
}

/** Turn on drawing some effects at certain position to visualize the radius.
 * \note Pass a negative radius to turn this off
 */
void showRangeAtPos(int centerX, int centerY, int radius)
{
	rangeCenterX = centerX;
	rangeCenterY = centerY;
	rangeRadius = radius;

	bRangeDisplay = true;

	if (radius <= 0) {
		bRangeDisplay = false;
	}
}

/// Get the graphic ID for a droid rank
unsigned getDroidRankGraphic(const Droid* psDroid)
{
	auto gfxId = UDWORD_MAX;

	/* Establish the numerical value of the droid's rank */
	switch (getDroidLevel(psDroid)) {
	case 0:
		break;
	case 1:
		gfxId = IMAGE_LEV_0;
		break;
	case 2:
		gfxId = IMAGE_LEV_1;
		break;
	case 3:
		gfxId = IMAGE_LEV_2;
		break;
	case 4:
		gfxId = IMAGE_LEV_3;
		break;
	case 5:
		gfxId = IMAGE_LEV_4;
		break;
	case 6:
		gfxId = IMAGE_LEV_5;
		break;
	case 7:
		gfxId = IMAGE_LEV_6;
		break;
	case 8:
		gfxId = IMAGE_LEV_7;
		break;
	default:
		ASSERT(!"out of range droid rank", "Weird droid level in drawDroidRank");
		break;
	}
	return gfxId;
}

/**	
 * Will render a graphic depiction of the droid's present rank.
 * @note Assumes matrix context set and that z-buffer write is force enabled (Always).
 */
static void drawDroidRank(Droid* psDroid)
{
	auto gfxId = getDroidRankGraphic(psDroid);

	/* Did we get one? - We should have... */
	if (gfxId != UDWORD_MAX) {
		/* Render the rank graphic at the correct location */ // remove hardcoded numbers?!
		iV_DrawImage(IntImages, (UWORD)gfxId,
                 psDroid->getDisplayData()->screen_x + psDroid->getDisplayData()->screen_r + 8,
                 psDroid->getDisplayData()->screen_y + psDroid->getDisplayData()->screen_r);
	}
}

/**	Will render a sensor graphic for a droid locked to a sensor droid/structure
 * \note Assumes matrix context set and that z-buffer write is force enabled (Always).
 */
static void drawDroidSensorLock(Droid* psDroid)
{
	//if on fire support duty - must be locked to a Sensor Droid/Structure
	if (orderState(psDroid, ORDER_TYPE::FIRE_SUPPORT))
	{
		/* Render the sensor graphic at the correct location - which is what?!*/
		iV_DrawImage(IntImages, IMAGE_GN_STAR, 
                 psDroid->getDisplayData()->screen_x, 
                 psDroid->getDisplayData()->screen_y);
	}
}

/// Draw the construction lines for all construction droids
static void doConstructionLines(const glm::mat4& viewMatrix)
{
	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		for (auto& psDroid : playerList[i].droids)
		{
			if (clipXY(psDroid.getPosition().x, psDroid.getPosition().y)
				&& psDroid.isVisibleToSelectedPlayer() == UBYTE_MAX
				&& psDroid.getMovementData()->status != MOVE_STATUS::SHUFFLE) {
				if (psDroid.getAction() == ACTION::BUILD) {
					if (psDroid.getOrder()->target) {
						if (auto psStruct = dynamic_cast<Structure*>(psDroid.getOrder()->target)) {
							addConstructionLine(&psDroid, psStruct, viewMatrix);
						}
					}
				}
				else if ((psDroid.getAction() == ACTION::DEMOLISH) ||
					(psDroid.getAction() == ACTION::REPAIR) ||
					(psDroid.getAction() == ACTION::RESTORE)) {
					if (dynamic_cast<const Structure*>(psDroid.getTarget(0))) {
						addConstructionLine(&psDroid, dynamic_cast<const Structure*>(psDroid.getTarget(0)), viewMatrix);
					}
				}
			}
		}
	}
}

static unsigned randHash(std::initializer_list<unsigned> data)
{
	uint32_t v = 0x12345678;
	auto shuffle = [&v](uint32_t d, uint32_t x)
	{
		v += d;
		v *= x;
		v ^= v >> 15;
		v *= 0x987decaf;
		v ^= v >> 17;
	};
	for (int i : data)
	{
		shuffle(i, 0x7ea99999);
	}
	for (int i : data)
	{
		shuffle(i, 0xc0ffee77);
	}
	return v;
}

/// Draw the construction or demolish lines for one droid
static void addConstructionLine(Droid* psDroid, Structure* psStructure, const glm::mat4& viewMatrix)
{
	auto deltaPlayer = Vector3f(-playerPos.p.x, 0, playerPos.p.z);
	auto pt0 = Vector3f(psDroid->getPosition().x, 
                      psDroid->getPosition().z + 24,
                      -psDroid->getPosition().y) + deltaPlayer;

	auto constructPoints = constructorPoints(dynamic_cast<ConstructStats const*>(
              psDroid->getComponent(COMPONENT_TYPE::CONSTRUCT)), psDroid->playerManager->getPlayer());
  
	auto amount = 800 * constructPoints * (graphicsTime - psDroid->getTimeActionStarted()) / GAME_TICKS_PER_SEC;

	Vector3i each;
	auto getPoint = [&](uint32_t c)
	{
		auto t = (amount + c) / 1000;
		auto s = (amount + c) % 1000 * .001f;
		auto pointIndexA = randHash({psDroid->getId(), 
                                      psStructure->getId(), 
                                      psDroid->getTimeActionStarted(), t, c}) % psStructure->
			getDisplayData()->imd_shape->points.size();
		auto pointIndexB = randHash({psDroid->getId(), psStructure->getId(), psDroid->getTimeActionStarted(), t + 1, c}) % psStructure
			->getDisplayData()->imd_shape->points.size();
		auto& pointA = psStructure->getDisplayData()->imd_shape->points[pointIndexA];
		auto& pointB = psStructure->getDisplayData()->imd_shape->points[pointIndexB];
		auto point = mix(pointA, pointB, s);

		each = Vector3f(psStructure->getPosition().x, psStructure->getPosition().z, psStructure->getPosition().y)
			+ Vector3f(point.x, structHeightScale(psStructure) * point.y, -point.z);
		return Vector3f(each.x, each.y, -each.z) + deltaPlayer;
	};

	auto pt1 = getPoint(250);
	auto pt2 = getPoint(750);

	if (psStructure->getCurrentBuildPoints() < 10) {
		auto pointC = Vector3f(psStructure->getPosition().x,
                           psStructure->getPosition().z + 10,
                           -psStructure->getPosition().y) + deltaPlayer;

		auto cross = Vector3f(psStructure->getPosition().y - psDroid->getPosition().y,
                          0, psStructure->getPosition().x - psDroid->getPosition().x);

		auto shift = 40.f * normalize(cross);
		pt1 = mix(pointC - shift, pt1, psStructure->getCurrentBuildPoints() * .1f);
		pt2 = mix(pointC + shift, pt1, psStructure->getCurrentBuildPoints() * .1f);
	}

	if (rand() % 250u < deltaGraphicsTime) {
		effectSetSize(30);
		addEffect(&each, EFFECT_GROUP::EXPLOSION, 
              EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED, 
              true, getImdFromIndex(MI_PLASMA), 0);
	}

	PIELIGHT colour = psDroid->getAction() == ACTION::DEMOLISH
          ? WZCOL_DEMOLISH_BEAM 
          : WZCOL_CONSTRUCTOR_BEAM;
  
	pie_TransColouredTriangle({pt0, pt1, pt2}, colour, viewMatrix);
	pie_TransColouredTriangle({pt0, pt2, pt1}, colour, viewMatrix);
}
