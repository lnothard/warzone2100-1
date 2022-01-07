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

#include "display.h"
#include "display3ddef.h"	// This should be the only place including this file
#include "lib/ivis_opengl/pietypes.h"
#include "lib/ivis_opengl/piedef.h"
#include "objectdef.h"
#include "message.h"

#define HEIGHT_TRACK_INCREMENTS (50)

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
void setViewPos(UDWORD x, UDWORD y, bool Pan);
Vector2i getPlayerPos();
void setPlayerPos(SDWORD x, SDWORD y);
void disp3d_setView(iView* newView);
void disp3d_oldView(); // for save games <= 10
void disp3d_getView(iView* newView);
void screenCoordToWorld(const Vector2i, Vector2i&, SDWORD&, SDWORD&);
void draw3DScene();
void renderStructure(Structure* psStructure, const glm::mat4& viewMatrix);
void renderFeature(Feature* psFeature, const glm::mat4& viewMatrix);
void renderProximityMsg(PROXIMITY_DISPLAY* psProxDisp, const glm::mat4& viewMatrix);
void renderProjectile(Projectile* psCurr, const glm::mat4& viewMatrix);
void renderDeliveryPoint(FLAG_POSITION* psPosition, bool blueprint, const glm::mat4& viewMatrix);

void calcScreenCoords(Droid* psDroid, const glm::mat4& viewMatrix);
ENERGY_BAR toggleEnergyBars();
void drawDroidSelection(Droid* psDroid, bool drawBox);

bool doWeDrawProximitys();
void setProximityDraw(bool val);

bool clipXY(SDWORD x, SDWORD y);
inline bool clipShapeOnScreen(const iIMDShape* pIMD, const glm::mat4& viewModelMatrix, int overdrawScreenPoints = 10);
bool clipDroidOnScreen(Droid* psDroid, const glm::mat4& viewModelMatrix, int overdrawScreenPoints = 25);
bool clipStructureOnScreen(Structure* psStructure, const glm::mat4& viewModelMatrix, int overdrawScreenPoints = 0);

bool init3DView();
void shutdown3DView();
extern iView playerPos;
extern bool selectAttempt;

extern SDWORD scrollSpeed;
void assignSensorTarget(SimpleObject* psObj);
void assignDestTarget();
UDWORD getWaterTileNum();
void setUnderwaterTile(UDWORD num);
UDWORD getRubbleTileNum();
void setRubbleTile(UDWORD num);

Structure* getTileBlueprintStructure(int mapX, int mapY);
///< Gets the blueprint at those coordinates, if any. Previous return value becomes invalid.
StructureStats const* getTileBlueprintStats(int mapX, int mapY);
///< Gets the structure stats of the blueprint at those coordinates, if any.
bool anyBlueprintTooClose(StructureStats const* stats, Vector2i pos, uint16_t dir);
///< Checks if any blueprint is too close to the given structure.
void clearBlueprints();

void display3dScreenSizeDidChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth,
                                  unsigned int newHeight);

extern SDWORD mouseTileX, mouseTileY;
extern Vector2i mousePos;

extern bool bRender3DOnly;
extern bool showGateways;
extern bool showPath;
extern const Vector2i visibleTiles;

/*returns the graphic ID for a droid rank*/
UDWORD getDroidRankGraphic(Droid* psDroid);

void setSkyBox(const char* page, float mywind, float myscale);

#define	BASE_MUZZLE_FLASH_DURATION	(GAME_TICKS_PER_SEC/10)
#define	EFFECT_MUZZLE_ADDITIVE		128

extern UWORD barMode;

extern bool CauseCrash;

extern bool tuiTargetOrigin;

/// Draws using the animation systems. Usually want to use in a while loop to get all model levels.
bool drawShape(SimpleObject* psObj, iIMDShape* strImd, int colour, PIELIGHT buildingBrightness, int pieFlag,
               int pieFlagData, const glm::mat4& viewMatrix);

int calculateCameraHeightAt(int tileX, int tileY);

#endif // __INCLUDED_SRC_DISPLAY3D_H__
