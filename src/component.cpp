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
 * @file component.cpp
 * Draws component objects
*/

#include "lib/netplay/netplay.h"
#include <glm/gtx/transform.hpp>

#ifndef GLM_ENABLE_EXPERIMENTAL
  #define GLM_ENABLE_EXPERIMENTAL
#endif

#include "action.h"
#include "component.h"
#include "display3d.h"
#include "displaydef.h"
#include "effects.h"
#include "faction.h"
#include "intdisplay.h"
#include "map.h"
#include "miscimd.h"
#include "projectile.h"
#include "transporter.h"

/* forward decl */
bool missionIsOffworld();
bool gamePaused();


#define GetRadius(x) ((x)->sradius)

static constexpr auto DEFAULT_COMPONENT_TRANSLUCENCY = 128;
static constexpr auto VTOL_CONNECTOR_START = 5;
static const auto DROID_EMP_SPREAD= 20 - rand() % 40;

static bool leftFirst;


// Colour Lookups
// use col = MAX_PLAYERS for anycolour (see multiint.c)
bool setPlayerColour(unsigned player, int col)
{
	if (player >= MAX_PLAYERS) {
		NetPlay.players[player].colour = MAX_PLAYERS;
		return true;
	}
	ASSERT_OR_RETURN(false, col < MAX_PLAYERS, "Bad colour setting");
	NetPlay.players[player].colour = col;
	return true;
}

uint8_t getPlayerColour(unsigned pl)
{
	if (pl == MAX_PLAYERS) {
		return 0; // baba
	}
	ASSERT_OR_RETURN(0, pl < MAX_PLAYERS, "Invalid player number %u", pl);
	return NetPlay.players[pl].colour;
}

static glm::mat4 setMatrix(const Vector3i* Position,
                           const Vector3i* Rotation, 
                           int scale)
{
	return glm::translate(glm::vec3(*Position)) *
		glm::rotate(UNDEG(DEG(Rotation->x)),
                glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->y)),
                glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->z)),
                glm::vec3(0.f, 0.f, 1.f)) *
		glm::scale(glm::vec3(scale / 100.f));
}

unsigned getComponentDroidRadius(Droid*)
{
	return 100;
}

unsigned getComponentDroidTemplateRadius(DroidTemplate*)
{
	return 100;
}

unsigned getComponentRadius(BaseStats* psComponent)
{
	iIMDShape* ComponentIMD = nullptr;
	iIMDShape* MountIMD = nullptr;

	auto compID = StatIsComponent(psComponent);
	if (compID != COMPONENT_TYPE::COUNT) {
		StatGetComponentIMD(psComponent, compID,
                        &ComponentIMD, &MountIMD);
		if (ComponentIMD) {
			return GetRadius(ComponentIMD);
		}
	}

	// vtol bombs are only stats allowed to have NULL ComponentIMD
	if (StatIsComponent(psComponent) != COMPONENT_TYPE::WEAPON
		|| (((WeaponStats*)psComponent)->weaponSubClass != WEAPON_SUBCLASS::BOMB
			&& ((WeaponStats*)psComponent)->weaponSubClass != WEAPON_SUBCLASS::EMP)) {
		ASSERT(ComponentIMD, "No ComponentIMD!");
	}
	return COMPONENT_RADIUS;
}

unsigned getResearchRadius(BaseStats* Stat)
{
	auto ResearchIMD = dynamic_cast<ResearchStats*>(Stat)->pIMD.get();
	if (ResearchIMD) {
		return GetRadius(ResearchIMD);
	}
	debug(LOG_ERROR, "ResearchPIE == NULL");
	return 100;
}


unsigned getStructureSizeMax(Structure* psStructure)
{
	// radius based on base plate size
	return MAX(psStructure->getStats().base_width,
             psStructure->getStats().base_breadth);
}

unsigned getStructureStatSizeMax(StructureStats* Stats)
{
	// radius based on base plate size
	return MAX(Stats->base_width, Stats->base_breadth);
}

unsigned getStructureStatHeight(StructureStats* psStat)
{
	if (psStat->IMDs[0]) {
		return (psStat->IMDs[0]->max.y - psStat->IMDs[0]->min.y);
	}
	return 0;
}

static void draw_player_3d_shape(unsigned player_index, iIMDShape* shape, 
                                 int frame, PIELIGHT colour, int pie_flag,
                                 int pie_flag_data, const glm::mat4& model_view)
{
	auto faction_shape = getFactionIMD(getPlayerFaction(player_index), shape);
	pie_Draw3DShape(faction_shape, frame, getPlayerColour(player_index), colour,
                  pie_flag, pie_flag_data, model_view);
}

void displayIMDButton(iIMDShape* IMDShape, const Vector3i* Rotation, 
                      const Vector3i* Position, int scale)
{
	draw_player_3d_shape(selectedPlayer, IMDShape, 0, WZCOL_WHITE, 
                       pie_BUTTON, 0, setMatrix(Position, Rotation, scale));
}

static void sharedStructureButton(StructureStats* Stats, iIMDShape* strImd, 
                                  const Vector3i* Rotation, const Vector3i* Position, int scale)
{
	Vector3i pos = *Position;

	// HACK HACK HACK!
  // if it's a 'tall thin (ie tower)' structure stat with something on 
	// the top - offset the position to show the object on top 
	if (strImd->nconnectors && scale == SMALL_STRUCT_SCALE &&
      getStructureStatHeight(Stats) > TOWER_HEIGHT) {
		pos.y -= 20;
	}

	const glm::mat4 matrix = setMatrix(&pos, Rotation, scale);

	// draw the building's base first
	auto baseImd = Stats->base_imd.get();

	if (baseImd) {
		draw_player_3d_shape(selectedPlayer, baseImd, 0,
                         WZCOL_WHITE, pie_BUTTON, 0, matrix);
	}

  // draw the turret
	draw_player_3d_shape(selectedPlayer, strImd, 0, 
                       WZCOL_WHITE, pie_BUTTON, 0, matrix);

  if (!strImd->nconnectors) {
    return;
  }

  iIMDShape *mountImd[MAX_WEAPONS], *weaponImd[MAX_WEAPONS];
  weaponImd[0] = nullptr;
  mountImd[0] = nullptr;
  for (auto i = 0; i < Stats->numWeaps; i++)
  {
    weaponImd[i] = nullptr; //weapon is gun ecm or sensor
    mountImd[i] = nullptr;
  }
  //get an imd to draw on the connector priority is weapon, ECM, sensor
  //check for weapon
  //can only have the MAX_WEAPONS
  for (auto i = 0; i < MAX(1, Stats->numWeaps); i++)
  {
    //can only have the one
    if (Stats->psWeapStat[i] != nullptr) {
      weaponImd[i] = Stats->psWeapStat[i]->pIMD.get();
      mountImd[i] = Stats->psWeapStat[i]->pMountGraphic.get();
    }

    if (!weaponImd[i]) {
      //check for ECM
      if (Stats->ecm_stats != nullptr) {
        weaponImd[i] = Stats->ecm_stats->pIMD.get();
        mountImd[i] = Stats->ecm_stats->pMountGraphic.get();
      }
    }

    if (!weaponImd[i]) {
      //check for sensor
      if (Stats->sensor_stats != nullptr) {
        weaponImd[i] = Stats->sensor_stats->pIMD.get();
        mountImd[i] = Stats->sensor_stats->pMountGraphic.get();
      }
    }
  }

  // draw Weapon/ECM/Sensor for structure
  if (!weaponImd[0]) {
    return;
  }
  
  for (auto i = 0; i < MAX(1, Stats->numWeaps); i++)
  {
    glm::mat4 localMatrix = glm::translate(glm::vec3(strImd->connectors[i].xzy()));
    if (mountImd[i] != nullptr) {
      draw_player_3d_shape(selectedPlayer, mountImd[i], 0,
                           WZCOL_WHITE, pie_BUTTON, 0,
                           matrix * localMatrix);
      if (mountImd[i]->nconnectors) {
        localMatrix *= glm::translate(glm::vec3(mountImd[i]->connectors->xzy()));
      }
    }
    // we have a droid weapon so do we draw a muzzle flash
    draw_player_3d_shape(selectedPlayer, weaponImd[i], 0,
                         WZCOL_WHITE, pie_BUTTON, 0, 
                         matrix * localMatrix);
  }
}

void displayStructureButton(Structure* psStructure, const Vector3i* rotation, 
                            const Vector3i* Position, int scale)
{
	sharedStructureButton(&psStructure->getStats(),
                        psStructure->getDisplayData().imd_shape.get(),
                        rotation, Position, scale);
}

void displayStructureStatButton(StructureStats* Stats, const Vector3i* rotation,
                                const Vector3i* Position, int scale)
{
	sharedStructureButton(Stats, Stats->IMDs[0].get(), 
                        rotation, Position, scale);
}

// Render a component given a BASE_STATS structure.
//
void displayComponentButton(BaseStats* Stat, const Vector3i* Rotation,
                            const Vector3i* Position, int scale)
{
	iIMDShape* ComponentIMD = nullptr;
	iIMDShape* MountIMD = nullptr;
	auto compID = StatIsComponent(Stat);

  if (compID < 0) {
    return;
  } else {
    StatGetComponentIMD(Stat, compID, &ComponentIMD, 
                        &MountIMD);
  }

  glm::mat4 matrix = setMatrix(Position, Rotation, scale);

  // vtol bombs are the only stats allowed to have NULL ComponentIMD
  if (StatIsComponent(Stat) != COMPONENT_TYPE::WEAPON
      || (((WeaponStats*)Stat)->weaponSubClass != WEAPON_SUBCLASS::BOMB
          && ((WeaponStats*)Stat)->weaponSubClass != WEAPON_SUBCLASS::EMP)) {
    ASSERT(ComponentIMD, "No ComponentIMD");
  }
  if (!MountIMD) {
    return;
  }

  draw_player_3d_shape(selectedPlayer, MountIMD, 0, 
                       WZCOL_WHITE, pie_BUTTON, 0, matrix);

  // translate for weapon mount point
  if (MountIMD->nconnectors) {
    matrix *= glm::translate(glm::vec3(MountIMD->connectors->xzy()));
  }
  if (ComponentIMD) {
    draw_player_3d_shape(selectedPlayer, ComponentIMD, 0, 
                         WZCOL_WHITE, pie_BUTTON, 0, matrix);
  }
}

void displayResearchButton(BaseStats* Stat, const Vector3i* Rotation, const Vector3i* Position, int scale)
{
	auto ResearchIMD = ((ResearchStats*)Stat)->pIMD.get();
	auto MountIMD = ((ResearchStats*)Stat)->pIMD2.get();

	ASSERT(ResearchIMD, "ResearchIMD is NULL");
  if (!ResearchIMD) {
    return;
  }
  const glm::mat4& matrix = setMatrix(Position, Rotation, scale);
  if (MountIMD) {
    draw_player_3d_shape(selectedPlayer, MountIMD, 0,
                         WZCOL_WHITE, pie_BUTTON, 0, matrix);
  }
  draw_player_3d_shape(selectedPlayer, ResearchIMD, 0, 
                       WZCOL_WHITE, pie_BUTTON, 0, matrix);
}


static iIMDShape* getLeftPropulsionIMD(Droid* psDroid)
{
	auto bodyStat = psDroid->asBits[COMP_BODY];
	auto propStat = psDroid->asBits[COMP_PROPULSION];
  
	return asBodyStats[bodyStat].ppIMDList[
          propStat * PROP_SIDE::COUNT + PROP_SIDE::LEFT];
}

static iIMDShape* getRightPropulsionIMD(Droid* psDroid)
{
	auto bodyStat = psDroid->asBits[COMP_BODY];
	auto propStat = psDroid->asBits[COMP_PROPULSION];
  
	return asBodyStats[bodyStat].ppIMDList[
          propStat * PROP_SIDE::COUNT + PROP_SIDE::RIGHT];
}

void drawMuzzleFlash(const Weapon& sWeap, const iIMDShape* weaponImd, iIMDShape* flashImd,
                     PIELIGHT buildingBrightness, int pieFlag, int iPieData,
                     const glm::mat4& viewMatrix, uint8_t colour)
{
	if (!weaponImd || !flashImd || !weaponImd->nconnectors ||
      graphicsTime < sWeap.timeLastFired) {
		return;
	}

	auto connector_num = 0;

	// which barrel is firing if model have multiple muzzle connectors?
	if (sWeap.shotsFired && (weaponImd->nconnectors > 1)) {
		// shoot first, draw later - subtract one shot to get correct results
		connector_num = (sWeap.shotsFired - 1) % (weaponImd->nconnectors);
	}

	// now we need to move to the end of the firing barrel
	const glm::mat4 modelMatrix = glm::translate(glm::vec3(
          weaponImd->connectors[connector_num].xzy()));

	// assume no clan colours for muzzle effects
	if (flashImd->numFrames == 0 || flashImd->animInterval <= 0) {
		// no anim so display one frame for a fixed time
		if (graphicsTime >= sWeap.timeLastFired &&
        graphicsTime < sWeap.timeLastFired + BASE_MUZZLE_FLASH_DURATION) {
			pie_Draw3DShape(flashImd, 0, colour, buildingBrightness, 
                      pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE,
			                viewMatrix * modelMatrix);
		}
	}
	else if (graphicsTime >= sWeap.timeLastFired) {
		// animated muzzle
		const auto DEFAULT_ANIM_INTERVAL = 17;
		// A lot of PIE files specify 1, which is too small, so set something bigger as a fallback
		auto animRate = MAX(flashImd->animInterval, DEFAULT_ANIM_INTERVAL);
		auto frame = (graphicsTime - sWeap.timeLastFired) / animRate;
		if (frame < flashImd->numFrames)
		{
			pie_Draw3DShape(flashImd, frame, colour, buildingBrightness, 
                      pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE,
			                viewMatrix * modelMatrix);
		}
	}
}

/* Assumes matrix context is already set */
// this is able to handle multiple weapon graphics now
// removed mountRotation,they get such stuff from psObj directly now
static bool displayCompObj(Droid* psDroid, bool bButton,
                           const glm::mat4& viewMatrix)
{
	int pieFlag, iPieData;
	PIELIGHT brightness;
	int colour;
	bool didDrawSomething = false;

	glm::mat4 modelMatrix(1.f);

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION && 
      psDroid->lastHitWeapon == WEAPON_SUBCLASS::ELECTRONIC && 
		  !gamePaused()) {
		colour = getPlayerColour(rand() % MAX_PLAYERS);
	}
	else {
		colour = getPlayerColour(psDroid->getPlayer());
	}

	/* get propulsion stats */
	auto psPropStats = psDroid->getPropulsion();

	// set pieflag for button object or in-game object
	if (bButton) {
		pieFlag = pie_BUTTON;
		brightness = WZCOL_WHITE;
	}
	else {
		pieFlag = pie_SHADOW;
		brightness = pal_SetBrightness(psDroid->illumination_level);
    
		// NOTE: Beware of transporters that are offscreen, on a mission!  
    // We should *not* be checking tiles at this point in time!
		if (!isTransporter(*psDroid) && !missionIsOffworld()) {
      auto psTile = worldTile(psDroid->getPosition().x,
                              psDroid->getPosition().y);
      
			if (psTile->jammerBits & alliancebits[psDroid->getPlayer()]) {
				pieFlag |= pie_ECM;
			}
		}
	}

	// set default components transparent
	if (psDroid->asBits[COMPONENT_TYPE::PROPULSION] == 0) {
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else {
		iPieData = 0;
	}

	if (!bButton && psPropStats->propulsionType == PROPULSION_TYPE::PROPELLOR) {
		// FIXME: change when adding submarines to the game
		modelMatrix *= glm::translate(glm::vec3(
            0.f, -world_coord(1) / 2.3f, 0.f));
	}

	iIMDShape* psShapeProp = (leftFirst
          ? getLeftPropulsionIMD(psDroid)
          : getRightPropulsionIMD(psDroid));

	if (psShapeProp) {
		if (pie_Draw3DShape(psShapeProp, 0, colour, brightness,
                        pieFlag, iPieData, viewMatrix * modelMatrix)) {
			didDrawSomething = true;
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMP_BODY] == 0) {
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else {
		pieFlag &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	/* Get the body graphic now*/
	iIMDShape* psShapeBody = BODY_IMD(psDroid, psDroid->getPlayer());
	if (psShapeBody) {
		iIMDShape* strImd = psShapeBody;
		if (psDroid->getType() == DROID_TYPE::PERSON) {
			modelMatrix *= glm::scale(glm::vec3(.75f)); // FIXME - hideous....!!!!
		}
		if (strImd->objanimpie[psDroid->animationEvent]) {
			strImd = psShapeBody->objanimpie[psDroid->animationEvent];
		}
		glm::mat4 viewModelMatrix = viewMatrix * modelMatrix;
		while (strImd)
		{
			if (drawShape(psDroid, strImd, colour, brightness,
                    pieFlag, iPieData, viewModelMatrix)) {
				didDrawSomething = true;
			}
			strImd = strImd->next;
		}
	}

	// render animation effects based on movement or lack thereof, if any
	auto psMoveAnim = asBodyStats[psDroid->asBits[COMP_BODY]]
          .ppMoveIMDList[psDroid->asBits[COMP_PROPULSION]];
	auto psStillAnim = asBodyStats[psDroid->asBits[COMP_BODY]]
          .ppStillIMDList[psDroid->asBits[COMP_PROPULSION]];
	glm::mat4 viewModelMatrix = viewMatrix * modelMatrix;
	if (!bButton && psMoveAnim && 
      psDroid->getMovementData().status != MOVE_STATUS::INACTIVE) {
		if (pie_Draw3DShape(psMoveAnim, getModularScaledGraphicsTime(
                                psMoveAnim->animInterval, psMoveAnim->numFrames),
                        colour, brightness, pie_ADDITIVE, 200, viewModelMatrix)) {
			didDrawSomething = true;
		}
	}
	else if (!bButton && psStillAnim) { //< standing still
		if (pie_Draw3DShape(psStillAnim,
		                    getModularScaledGraphicsTime(
                                psStillAnim->animInterval, psStillAnim->numFrames), colour,
                        brightness, 0, 0, viewModelMatrix)) {
			didDrawSomething = true;
		}
	}

	// don't change the screen coords of an object if drawing it in a button
	if (!bButton) {
		// set up all the screen coords stuff - need to REMOVE FROM THIS LOOP
		calcScreenCoords(psDroid, viewModelMatrix);
	}

	/* set default components transparent */
	if (psDroid->asWeaps[0].nStat == 0 &&
		psDroid->asBits[COMP_SENSOR] == 0 &&
		psDroid->asBits[COMP_ECM] == 0 &&
		psDroid->asBits[COMP_BRAIN] == 0 &&
		psDroid->asBits[COMP_REPAIRUNIT] == 0 &&
		psDroid->asBits[COMP_CONSTRUCT] == 0) {
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else {
		pieFlag &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

  int iConnector;
	if (psShapeBody && psShapeBody->nconnectors) {
		// vtol weapons attach to connector 2 (underneath). all others to 
    // connector 1 vtols now skip the first 5 connectors(0 to 4).
    // vtols use 5,6,7,8 etc. now
		if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT &&
    psDroid->getType() == DROID_TYPE::WEAPON) {
			iConnector = VTOL_CONNECTOR_START;
		}
		else {
			iConnector = 0;
		}

		switch (psDroid->getType()) {
      using enum DROID_TYPE;
		  case DEFAULT:
		  case TRANSPORTER:
		  case SUPER_TRANSPORTER:
		  case CYBORG:
		  case CYBORG_SUPER:
		  case WEAPON:
		  case COMMAND:
        // command droids have a weapon to store all the graphics get the mounting
        // graphic - we've already moved to the right position. allegedly, all droids
        // will have a mount graphic so this shouldn't fall on its arse... double check
        // that the weapon droid actually has any
		  	for (auto i = 0; i < numWeapons(*psDroid); i++)
		  	{
		  		if ((psDroid->asWeaps[i].nStat > 0 || psDroid->getType() == DEFAULT)
		  			&& psShapeBody->connectors) {
		  			Rotation rot = getInterpolatedWeaponRotation(psDroid, i, graphicsTime);

		  			glm::mat4 localModelMatrix = modelMatrix;

		  			//to skip number of VTOL_CONNECTOR_START ground unit connectors
		  			if (iConnector < VTOL_CONNECTOR_START) {
		  				localModelMatrix *= glm::translate(
                      glm::vec3(psShapeBody->connectors[i].xzy()));
		  			}
		  			else {
		  				localModelMatrix *= glm::translate(
                      glm::vec3(psShapeBody->connectors[iConnector + i].xzy()));
		  			}
		  			localModelMatrix *= glm::rotate(
                    UNDEG(-rot.direction),
                    glm::vec3(0.f, 1.f, 0.f));

		  			/* vtol weapons inverted */
		  			if (iConnector >= VTOL_CONNECTOR_START) {
		  				//this might affect gun rotation
		  				localModelMatrix *= glm::rotate(
                      UNDEG(65536 / 2),
                      glm::vec3(0.f, 0.f, 1.f));
		  			}

		  			/* Get the mount graphic */
		  			iIMDShape* psShape = WEAPON_MOUNT_IMD(psDroid, i);

		  			auto recoilValue = psDroid->getWeapons()[i].getRecoil();
		  			localModelMatrix *= glm::translate(
                    glm::vec3(0.f, 0.f, recoilValue / 3.f));

		  			/* Draw it */
		  			if (psShape) {
		  				if (pie_Draw3DShape(
                      psShape, 0, colour, brightness,
                      pieFlag, iPieData, viewMatrix * localModelMatrix)) {
		  					didDrawSomething = true;
		  				}
		  			}
		  			localModelMatrix *= glm::translate(
                    glm::vec3(0, 0, recoilValue));

		  			/* translate for weapon mount point */
		  			if (psShape && psShape->nconnectors) {
		  				localModelMatrix *= glm::translate(
                      glm::vec3(psShape->connectors->xzy()));
		  			}

		  			/* vtol weapons inverted */
		  			if (iConnector >= VTOL_CONNECTOR_START) {
		  				//pitch the barrel down
		  				localModelMatrix *= glm::rotate(
                      UNDEG(-rot.pitch), glm::vec3(1.f, 0.f, 0.f));
		  			}
		  			else {
		  				//pitch the barrel up
		  				localModelMatrix *= glm::rotate(
                      UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
		  			}

		  			/* Get the weapon (gun?) graphic */
		  			psShape = WEAPON_IMD(psDroid, i);

		  			// We have a weapon so we draw it and a muzzle flash from weapon connector
		  			if (psShape)
		  			{
		  				glm::mat4 localViewModelMatrix = viewMatrix * localModelMatrix;
		  				if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData, localViewModelMatrix))
		  				{
		  					didDrawSomething = true;
		  				}
		  				drawMuzzleFlash(psDroid->getWeapons()[i], psShape, MUZZLE_FLASH_PIE(psDroid, i), brightness, pieFlag,
		  				                iPieData, localViewModelMatrix);
		  			}
		  		}
		  	}
		  	break;
		  case SENSOR:
		  case CONSTRUCT:
		  case CYBORG_CONSTRUCT:
		  case ECM:
		  case REPAIRER:
		  case CYBORG_REPAIR:
		  	{
		  		Rotation rot = getInterpolatedWeaponRotation(psDroid, 0, graphicsTime);
		  		iIMDShape* psShape = nullptr;
		  		iIMDShape* psMountShape = nullptr;

		  		switch (psDroid->getType()) {
		  		  case SENSOR:
		  		  	psMountShape = SENSOR_MOUNT_IMD(psDroid, psDroid->player);
		  		  /* Get the sensor graphic, assuming it's there */
		  		  	psShape = SENSOR_IMD(psDroid, psDroid->player);
		  		  	break;
		  		  case CONSTRUCT:
		  		  case CYBORG_CONSTRUCT:
		  		  	psMountShape = CONSTRUCT_MOUNT_IMD(psDroid, psDroid->player);
		  		  /* Get the construct graphic assuming it's there */
		  		  	psShape = CONSTRUCT_IMD(psDroid, psDroid->player);
		  		  	break;
		  		  case ECM:
		  		  	psMountShape = ECM_MOUNT_IMD(psDroid, psDroid->player);
		  		  /* Get the ECM graphic assuming it's there.... */
		  		  	psShape = ECM_IMD(psDroid, psDroid->player);
		  		  	break;
		  		  case REPAIRER:
		  		  case CYBORG_REPAIR:
		  		  	psMountShape = REPAIR_MOUNT_IMD(psDroid, psDroid->player);
		  		  /* Get the Repair graphic assuming it's there.... */
		  		  	psShape = REPAIR_IMD(psDroid, psDroid->player);
		  		  	break;
            default:
              ASSERT(false, "Bad component type");
              break;
		  		}
		  		/*	Get the mounting graphic - we've already moved to the right position
		  		Allegedly - all droids will have a mount graphic so this shouldn't
		  		fall on it's arse......*/
		  		//sensor and cyborg and ecm uses connectors[0]

		  		glm::mat4 localModelMatrix = modelMatrix;
		  		/* vtol weapons inverted */
		  		if (iConnector >= VTOL_CONNECTOR_START) {
		  			//this might affect gun rotation
		  			localModelMatrix *= glm::rotate(
                    UNDEG(65536 / 2), glm::vec3(0.f, 0.f, 1.f));
		  		}

		  		localModelMatrix *= glm::translate(glm::vec3(psShapeBody->connectors[0].xzy()));

		  		localModelMatrix *= glm::rotate(
                  UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));
		  		/* Draw it */
		  		if (psMountShape) {
		  			if (pie_Draw3DShape(psMountShape, 0, colour,
                                brightness, pieFlag, iPieData,
		  			                    viewMatrix * localModelMatrix)) {
		  				didDrawSomething = true;
		  			}
		  		}

		  		/* translate for construct mount point if cyborg */
		  		if (isCyborg(psDroid) && psMountShape && psMountShape->nconnectors) {
		  			localModelMatrix *= glm::translate(
                    glm::vec3(psMountShape->connectors[0].xzy()));
		  		}

		  		/* Draw it */
		  		if (psShape) {
		  			if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData,
		  			                    viewMatrix * localModelMatrix)) {
		  				didDrawSomething = true;
		  			}

		  			// in repair droid case only:
		  			if ((psDroid->getType() == REPAIRER ||
                 psDroid->getType() == CYBORG_REPAIR) &&
		  				 	 psShape->nconnectors &&
                 psDroid->getAction() == ACTION::REPAIR) {
		  				auto st = interpolateObjectSpacetime(psDroid, graphicsTime);
		  				localModelMatrix *= glm::translate(glm::vec3(psShape->connectors[0].xzy()));
		  				localModelMatrix *= glm::translate(glm::vec3(0.f, -20.f, 0.f));

		  				psShape = getImdFromIndex(MI_FLAME);

		  				/* Rotate for droid */
		  				localModelMatrix *= glm::rotate(
                      UNDEG(st.rotation.direction),
                      glm::vec3(0.f, 1.f, 0.f));
		  				localModelMatrix *= glm::rotate(
                      UNDEG(-st.rotation.pitch),
                      glm::vec3(1.f, 0.f, 0.f));
		  				localModelMatrix *= glm::rotate(
                      UNDEG(-st.rotation.roll),
                      glm::vec3(0.f, 0.f, 1.f));
		  				// rotate Y
		  				localModelMatrix *= glm::rotate(
                      UNDEG(rot.direction),
                      glm::vec3(0.f, 1.f, 0.f));

		  				localModelMatrix *= glm::rotate(
                      UNDEG(-playerPos.r.y),
                      glm::vec3(0.f, 1.f, 0.f));
		  				localModelMatrix *= glm::rotate(
                      UNDEG(-playerPos.r.x),
                      glm::vec3(1.f, 0.f, 0.f));

		  				if (pie_Draw3DShape(
                      psShape, getModularScaledGraphicsTime(
                              psShape->animInterval, psShape->numFrames),
                              0, brightness, pie_ADDITIVE, 140,
                      viewMatrix * localModelMatrix)) {
                didDrawSomething = true;
              }
		  			}
		  		}
		  		break;
		  	}
		  case PERSON:
		  	// no extra mounts for people
		  	break;
		  default:
		  	ASSERT(!"invalid droid type", "Whoa! Weirdy type of droid found in drawComponentObject!!!");
		  	break;
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMP_PROPULSION] == 0) {
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else {
		pieFlag &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	// now render the other propulsion side
	psShapeProp = (leftFirst ? getRightPropulsionIMD(psDroid) : getLeftPropulsionIMD(psDroid));
	if (psShapeProp)
	{
		if (pie_Draw3DShape(psShapeProp, 0, colour,
                        brightness, pieFlag, iPieData,
                        viewModelMatrix)) {
      // safe to use viewModelMatrix because modelView has not been
      // changed since it was calculated
			didDrawSomething = true;
		}
	}

	return didDrawSomething;
}

void displayComponentButtonTemplate(DroidTemplate* psTemplate, const Vector3i* Rotation,
                                    const Vector3i* Position, int scale)
{
	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// Decide how to sort it.
	leftFirst = angleDelta(DEG(Rotation->y)) < 0;

	auto droid = std::make_unique<Droid>(0, selectedPlayer);
	memset(droid->asBits, 0, sizeof(droid->asBits));
	droidSetBits(psTemplate, droid.get());

	droid->setPosition({0, 0, 0});
	droid->setRotation({0, 0, 0});

	//draw multi component object as a button object
	displayCompObj(droid.get(), true, matrix);
}

void displayComponentButtonObject(Droid* psDroid, const Vector3i* Rotation, const Vector3i* Position, int scale)
{
	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// decide how to sort it.
	auto difference = Rotation->y % 360;

	leftFirst = !((difference > 0 && difference < 180) || difference < -180);

	// And render the composite object.
	//draw multi component object as a button object
	displayCompObj(psDroid, true, matrix);
}

/* Assumes matrix context is already set */
// multiple turrets display removed the pointless mountRotation
void displayComponentObject(Droid* psDroid, const glm::mat4& viewMatrix)
{
	Vector3i position, rotation;
	Spacetime st = interpolateObjectSpacetime(psDroid, graphicsTime);

	leftFirst = angleDelta(playerPos.r.y - st.rotation.direction) <= 0;

	/* Get the real position */
	position.x = st.position.x - playerPos.p.x;
	position.z = -(st.position.y - playerPos.p.z);
	position.y = st.position.z;

	if (isTransporter(*psDroid)) {
		position.y += bobTransporterHeight();
	}

	/* Get all the pitch,roll,yaw info */
	rotation.y = -st.rotation.direction;
	rotation.x = st.rotation.pitch;
	rotation.z = st.rotation.roll;

	/* Translate origin */
	/* Rotate for droid */
	glm::mat4 modelMatrix = glm::translate(glm::vec3(position)) *
		glm::rotate(UNDEG(rotation.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(rotation.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(rotation.z), glm::vec3(0.f, 0.f, 1.f));

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION &&
      psDroid->lastHitWeapon == WEAPON_SUBCLASS::ELECTRONIC) {
		modelMatrix *= objectShimmy((PersistentObject*)psDroid);
	}

	// now check if the projected circle is within the screen boundaries
	if (!clipDroidOnScreen(psDroid, viewMatrix * modelMatrix)) {
		return;
	}

	if (psDroid->lastHitWeapon == WEAPON_SUBCLASS::EMP &&
      graphicsTime - psDroid->timeLastHit < EMP_DISABLE_TIME) {
		Vector3i effectPosition;

		//add an effect on the droid
		effectPosition.x = st.position.x + DROID_EMP_SPREAD;
		effectPosition.y = st.position.z + rand() % 8;
		effectPosition.z = st.position.y + DROID_EMP_SPREAD;
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&effectPosition, EFFECT_GROUP::EXPLOSION,
              EFFECT_TYPE::EXPLOSION_TYPE_PLASMA, false, nullptr, 0);
	}

	if (psDroid->visibleToSelectedPlayer() == UBYTE_MAX) {
		//ingame not button object
		//should render 3 mounted weapons now
		if (displayCompObj(psDroid, false, viewMatrix * modelMatrix)) {
			// did draw something to the screen - update the framenumber
			psDroid->display.frame_number = frameGetFrameNumber();
		}
	}
	else
	{
		auto frame = graphicsTime / BLIP_ANIM_DURATION + psDroid->getId() % 8192;
		// de-sync the blip effect, but don't overflow the int
		if (pie_Draw3DShape(getImdFromIndex(MI_BLIP), frame,
                        0, WZCOL_WHITE, pie_ADDITIVE,
		                    psDroid->visibleToSelectedPlayer() / 2,
                        viewMatrix * modelMatrix)) {
			psDroid->display.frame_number = frameGetFrameNumber();
		}
	}
}

void destroyFXDroid(Droid* psDroid, unsigned impactTime)
{
	for (auto i = 0; i < 5; ++i)
	{
		iIMDShape* psImd = nullptr;

		auto maxHorizontalScatter = TILE_UNITS / 4;
		auto heightScatter = TILE_UNITS / 5;
		auto horizontalScatter = iSinCosR(rand(), rand() % maxHorizontalScatter);

		auto pos = (
            psDroid->getPosition() + Vector3i(
                    horizontalScatter, 16 + heightScatter)).xzy();
		switch (i) {
		  case 0:
		  	switch (psDroid->getType()) {
          using enum DROID_TYPE;
		  	  case DEFAULT:
		  	  case CYBORG:
		  	  case CYBORG_SUPER:
		  	  case CYBORG_CONSTRUCT:
		  	  case CYBORG_REPAIR:
		  	  case WEAPON:
		  	  case COMMAND:
		  	  	if (numWeapons(*psDroid) > 0) {
		  	  		if (psDroid->asWeaps[0].nStat > 0) {
		  	  			psImd = WEAPON_MOUNT_IMD(psDroid, 0);
		  	  		}
		  	  	}
		  	  	break;
		    	default:
		    		break;
		  	}
		  	break;
		  case 1:
		  	switch (psDroid->getType()) {
          using enum DROID_TYPE;
		  	  case DEFAULT:
		  	  case CYBORG:
		  	  case CYBORG_SUPER:
		  	  case CYBORG_CONSTRUCT:
		  	  case CYBORG_REPAIR:
		  	  case WEAPON:
		  	  case COMMAND:
		  	  	if (numWeapons(*psDroid) > 0) {
		  	  		// get main weapon
		  	  		psImd = WEAPON_IMD(psDroid, 0);
		  	  	}
		  	  	break;
		  	  default:
		  	  	break;
			}
			break;
		}

		if (!psImd) {
			psImd = getRandomDebrisImd();
		}
		// tell the effect system that it needs to use this player's color for the next effect
		SetEffectForPlayer(psDroid->getPlayer());

		addEffect(&pos, EFFECT_GROUP::GRAVITON,
              EFFECT_TYPE::GRAVITON_TYPE_EMITTING_DR, true,
              psImd, getPlayerColour(psDroid->getPlayer()), impactTime);
	}
}

void compPersonToBits(Droid* psDroid)
{
  // display only - should not affect game state
	if (!psDroid->visibleToSelectedPlayer()) {
		// we can't see the person or cyborg - so get out
		return;
	}

  // get bits pointers according to whether baba or cyborg
  iIMDShape *headImd, *legsImd, *armImd, *bodyImd;
	if (isCyborg(psDroid)) {
		// this is probably unused now, since there's a more
    // appropriate effect for cyborgs
		headImd = getImdFromIndex(MI_CYBORG_HEAD);
		legsImd = getImdFromIndex(MI_CYBORG_LEGS);
		armImd = getImdFromIndex(MI_CYBORG_ARM);
		bodyImd = getImdFromIndex(MI_CYBORG_BODY);
	}
	else {
		headImd = getImdFromIndex(MI_BABA_HEAD);
		legsImd = getImdFromIndex(MI_BABA_LEGS);
		armImd = getImdFromIndex(MI_BABA_ARM);
		bodyImd = getImdFromIndex(MI_BABA_BODY);
	}

	/* Get where he's at */
	auto x = psDroid->getPosition().x;
	auto y = psDroid->getPosition().z + 1;
	auto z = psDroid->getPosition().y;
  auto position = Position{x, y, z};

	/* Tell about player colour */
	auto col = getPlayerColour(psDroid->getPlayer());

	addEffect(&position, EFFECT_GROUP::GRAVITON, 
            EFFECT_TYPE::GRAVITON_TYPE_GIBLET, true,
            headImd, col, gameTime - deltaGameTime + 1);
  
	addEffect(&position, EFFECT_GROUP::GRAVITON, 
            EFFECT_TYPE::GRAVITON_TYPE_GIBLET, true,
            legsImd, col, gameTime - deltaGameTime + 1);
  
	addEffect(&position, EFFECT_GROUP::GRAVITON, 
            EFFECT_TYPE::GRAVITON_TYPE_GIBLET, true,
            armImd, col, gameTime - deltaGameTime + 1);
  
	addEffect(&position, EFFECT_GROUP::GRAVITON, 
            EFFECT_TYPE::GRAVITON_TYPE_GIBLET, true,
            bodyImd, col, gameTime - deltaGameTime + 1);
  
}

int rescaleButtonObject(int radius, int baseScale, int baseRadius)
{
	auto newScale = 100 * baseRadius;
	newScale /= radius;
	if (baseScale > 0)
	{
		newScale += baseScale;
		newScale /= 2;
	}
	return newScale;
}
