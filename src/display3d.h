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

#ifndef __INCLUDED_SRC_DISPLAY3D_H__
#define __INCLUDED_SRC_DISPLAY3D_H__

#include <cstdint>

#include <glm/fwd.hpp>
#include "lib/framework/fixedpoint.h"
#include "lib/framework/vector.h"
#include "lib/ivis_opengl/pietypes.h"

#include "console.h"
#include "hci.h"

class BaseObject;
class Droid;
class Feature;
class FlagPosition;
class iIMDShape;
class Projectile;
class PROXIMITY_DISPLAY;
class Structure;
class StructureStats;


static constexpr auto TILE_WIDTH = 128;
static constexpr auto TILE_HEIGHT = 128;
static constexpr auto TILE_SIZE = TILE_WIDTH * TILE_HEIGHT;

// Amount of visible terrain tiles in x/y direction
static constexpr auto VISIBLE_XTILES = 64;
static constexpr auto VISIBLE_YTILES = 64;

static const auto RADTLX = OBJ_BACKX + OBJ_BACKWIDTH + BASE_GAP + 1 + D_W;
static const auto RADTLY  = RET_Y + 1;
static constexpr auto RADWIDTH	= 128;
static constexpr auto RADHEIGHT	= 128;

static constexpr auto SKY_MULT	= 1;
static const auto SKY_SHIMMY_BASE	= ((DEG(1)*SKY_MULT)/2);
static const auto SKY_SHIMMY = SKY_SHIMMY_BASE - (rand() % (2 * SKY_SHIMMY_BASE));

static constexpr auto HEIGHT_TRACK_INCREMENTS = 50;

/*!
 * Special tile types
 */
enum TILE_ID
{
	RIVERBED_TILE = 5,
	//! Underwater ground
	WATER_TILE = 17,
	//! Water surface
	RUBBLE_TILE = 54,
	//! You can drive over these
	BLOCKING_RUBBLE_TILE = 67 //! You cannot drive over these
};

enum ENERGY_BAR
{
	BAR_SELECTED,
	BAR_DROIDS,
	BAR_DROIDS_AND_STRUCTURES,
	BAR_LAST
};

struct iView
{
	Vector3i p = Vector3i(0, 0, 0);
	Vector3i r = Vector3i(0, 0, 0);
};

extern bool showFPS;
extern bool showUNITCOUNT;
extern bool showSAMPLES;
extern bool showORDERS;

extern int BlueprintTrackAnimationSpeed;

float getViewDistance();
void setViewDistance(float dist);
extern bool radarOnScreen;
extern bool radarPermitted;
bool radarVisible();

extern bool rangeOnScreen; // Added to get sensor/gun range on screen.  -Q 5-10-05
void setViewPos(unsigned x, unsigned y, bool Pan);
Vector2i getPlayerPos();
void setPlayerPos(int x, int y);
void disp3d_setView(iView* newView);
void disp3d_oldView(); // for save games <= 10
void disp3d_getView(iView* newView);
void screenCoordToWorld(Vector2i, Vector2i&, int&, int&);
void draw3DScene();
void renderStructure(Structure* psStructure, const glm::mat4& viewMatrix);
void renderFeature(Feature* psFeature, const glm::mat4& viewMatrix);
void renderProximityMsg(PROXIMITY_DISPLAY* psProxDisp, const glm::mat4& viewMatrix);
void renderProjectile(Projectile* psCurr, const glm::mat4& viewMatrix);
void renderDeliveryPoint(FlagPosition* psPosition, bool blueprint, const glm::mat4& viewMatrix);

void calcScreenCoords(Droid* psDroid, const glm::mat4& viewMatrix);
ENERGY_BAR toggleEnergyBars();
void drawDroidSelection(Droid* psDroid, bool drawBox);

bool doWeDrawProximitys();
void setProximityDraw(bool val);

bool clipXY(int x, int y);
inline bool clipShapeOnScreen(const iIMDShape* pIMD, const glm::mat4& viewModelMatrix, int overdrawScreenPoints = 10);
bool clipDroidOnScreen(Droid* psDroid, const glm::mat4& viewModelMatrix, int overdrawScreenPoints = 25);
bool clipStructureOnScreen(Structure* psStructure, const glm::mat4& viewModelMatrix, int overdrawScreenPoints = 0);

bool init3DView();
void shutdown3DView();
extern iView playerPos;
extern bool selectAttempt;

extern int scrollSpeed;
void assignSensorTarget(BaseObject* psObj);
void assignDestTarget();
unsigned getWaterTileNum();
void setUnderwaterTile(unsigned num);
unsigned getRubbleTileNum();
void setRubbleTile(unsigned num);

Structure* getTileBlueprintStructure(int mapX, int mapY);
///< Gets the blueprint at those coordinates, if any. Previous return value becomes invalid.
StructureStats getTileBlueprintStats(int mapX, int mapY);
///< Gets the structure stats of the blueprint at those coordinates, if any.
bool anyBlueprintTooClose(StructureStats const* stats, Vector2i pos, uint16_t dir);
///< Checks if any blueprint is too close to the given structure.
void clearBlueprints();

void display3dScreenSizeDidChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth,
                                  unsigned int newHeight);

extern int mouseTileX, mouseTileY;
extern Vector2i mousePos;

extern bool bRender3DOnly;
extern bool showGateways;
extern bool showPath;
extern const Vector2i visibleTiles;

/*returns the graphic ID for a droid rank*/
unsigned getDroidRankGraphic(Droid* psDroid);

void setSkyBox(const char* page, float mywind, float myscale);

#define	BASE_MUZZLE_FLASH_DURATION	(GAME_TICKS_PER_SEC/10)
#define	EFFECT_MUZZLE_ADDITIVE		128

extern uint16_t barMode;

extern bool CauseCrash;

extern bool tuiTargetOrigin;

/// Draws using the animation systems. Usually want to use in a while loop to get all model levels.
bool drawShape(BaseObject* psObj, iIMDShape* strImd, int colour, PIELIGHT buildingBrightness, int pieFlag,
               int pieFlagData, const glm::mat4& viewMatrix);

int calculateCameraHeightAt(int tileX, int tileY);

#endif // __INCLUDED_SRC_DISPLAY3D_H__
