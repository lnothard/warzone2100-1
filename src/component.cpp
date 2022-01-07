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

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/netplay/netplay.h"
#include <glm/gtx/transform.hpp>

#include "action.h"
#include "component.h"
#include "display3d.h"
#include "effects.h"
#include "intdisplay.h"
#include "loop.h"
#include "map.h"
#include "miscimd.h"
#include "projectile.h"
#include "transporter.h"
#include "mission.h"
#include "faction.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#define GetRadius(x) ((x)->sradius)

static constexpr auto DEFAULT_COMPONENT_TRANSLUCENCY = 128;
static const auto DROID_EMP_SPREAD= 20 - rand() % 40;

static constexpr auto VTOL_CONNECTOR_START = 5;

static bool leftFirst;

// Colour Lookups
// use col = MAX_PLAYERS for anycolour (see multiint.c)
bool setPlayerColour(unsigned player, unsigned col)
{
	if (player >= MAX_PLAYERS)
	{
		NetPlay.players[player].colour = MAX_PLAYERS;
		return true;
	}
	ASSERT_OR_RETURN(false, col < MAX_PLAYERS, "Bad colour setting");
	NetPlay.players[player].colour = col;
	return true;
}

uint8_t getPlayerColour(unsigned pl)
{
	if (pl == MAX_PLAYERS)
	{
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
		glm::rotate(UNDEG(DEG(Rotation->x)), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->y)), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->z)), glm::vec3(0.f, 0.f, 1.f)) *
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
	int compID;

	compID = StatIsComponent(psComponent);
	if (compID >= 0)
	{
		StatGetComponentIMD(psComponent, compID, &ComponentIMD, &MountIMD);
		if (ComponentIMD)
		{
			return GetRadius(ComponentIMD);
		}
	}

	/* VTOL bombs are only stats allowed to have NULL ComponentIMD */
	if (StatIsComponent(psComponent) != COMP_WEAPON
		|| (((WeaponStats*)psComponent)->weaponSubClass != WEAPON_SUBCLASS::BOMB
			&& ((WeaponStats*)psComponent)->weaponSubClass != WEAPON_SUBCLASS::EMP))
	{
		ASSERT(ComponentIMD, "No ComponentIMD!");
	}

	return COMPONENT_RADIUS;
}

unsigned getResearchRadius(BaseStats* Stat)
{
	auto ResearchIMD = ((ResearchStats*)Stat)->pIMD.get();
	if (ResearchIMD) {
		return GetRadius(ResearchIMD);
	}
	debug(LOG_ERROR, "ResearchPIE == NULL");
	return 100;
}


unsigned getStructureSizeMax(Structure* psStructure)
{
	//radius based on base plate size
	return MAX(psStructure->pStructureType->base_width, psStructure->pStructureType->base_breadth);
}

unsigned getStructureStatSizeMax(StructureStats* Stats)
{
	//radius based on base plate size
	return MAX(Stats->base_width, Stats->base_breadth);
}

unsigned getStructureStatHeight(StructureStats* psStat)
{
	if (psStat->IMDs[0])
	{
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

static void sharedStructureButton(StructureStats* Stats, iIMDShape* strImd, const Vector3i* Rotation,
                                  const Vector3i* Position, int scale)
{
	iIMDShape *baseImd, *mountImd[MAX_WEAPONS], *weaponImd[MAX_WEAPONS];
	Vector3i pos = *Position;

	/* HACK HACK HACK!
	if its a 'tall thin (ie tower)' structure stat with something on the top - offset the position to show the object on top */
	if (strImd->nconnectors && scale == SMALL_STRUCT_SCALE && getStructureStatHeight(Stats) > TOWER_HEIGHT)
	{
		pos.y -= 20;
	}

	const glm::mat4 matrix = setMatrix(&pos, Rotation, scale);

	/* Draw the building's base first */
	baseImd = Stats->base_imd.get();

	if (baseImd != nullptr)
	{
		draw_player_3d_shape(selectedPlayer, baseImd, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
	}
	draw_player_3d_shape(selectedPlayer, strImd, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);

	//and draw the turret
	if (strImd->nconnectors)
	{
		weaponImd[0] = nullptr;
		mountImd[0] = nullptr;
		for (int i = 0; i < Stats->numWeaps; i++)
		{
			weaponImd[i] = nullptr; //weapon is gun ecm or sensor
			mountImd[i] = nullptr;
		}
		//get an imd to draw on the connector priority is weapon, ECM, sensor
		//check for weapon
		//can only have the MAX_WEAPONS
		for (int i = 0; i < MAX(1, Stats->numWeaps); i++)
		{
			//can only have the one
			if (Stats->psWeapStat[i] != nullptr)
			{
				weaponImd[i] = Stats->psWeapStat[i]->pIMD;
				mountImd[i] = Stats->psWeapStat[i]->pMountGraphic;
			}

			if (weaponImd[i] == nullptr)
			{
				//check for ECM
				if (Stats->ecm_stats != nullptr)
				{
					weaponImd[i] = Stats->ecm_stats->pIMD;
					mountImd[i] = Stats->ecm_stats->pMountGraphic;
				}
			}

			if (weaponImd[i] == nullptr)
			{
				//check for sensor
				if (Stats->sensor_stats != nullptr)
				{
					weaponImd[i] = Stats->sensor_stats->pIMD;
					mountImd[i] = Stats->sensor_stats->pMountGraphic;
				}
			}
		}

		//draw Weapon/ECM/Sensor for structure
		if (weaponImd[0] != nullptr)
		{
			for (int i = 0; i < MAX(1, Stats->numWeaps); i++)
			{
				glm::mat4 localMatrix = glm::translate(glm::vec3(strImd->connectors[i].xzy()));
				if (mountImd[i] != nullptr)
				{
					draw_player_3d_shape(selectedPlayer, mountImd[i], 0, WZCOL_WHITE, pie_BUTTON, 0,
					                     matrix * localMatrix);
					if (mountImd[i]->nconnectors)
					{
						localMatrix *= glm::translate(glm::vec3(mountImd[i]->connectors->xzy()));
					}
				}
				draw_player_3d_shape(selectedPlayer, weaponImd[i], 0, WZCOL_WHITE, pie_BUTTON, 0, matrix * localMatrix);
				//we have a droid weapon so do we draw a muzzle flash
			}
		}
	}
}

void displayStructureButton(Structure* psStructure, const Vector3i* rotation, const Vector3i* Position, int scale)
{
	sharedStructureButton(psStructure->pStructureType, psStructure->sDisplay.imd, rotation, Position, scale);
}

void displayStructureStatButton(StructureStats* Stats, const Vector3i* rotation, const Vector3i* Position, int scale)
{
	sharedStructureButton(Stats, Stats->IMDs[0], rotation, Position, scale);
}

// Render a component given a BASE_STATS structure.
//
void displayComponentButton(BaseStats* Stat, const Vector3i* Rotation, const Vector3i* Position, int scale)
{
	iIMDShape* ComponentIMD = nullptr;
	iIMDShape* MountIMD = nullptr;
	int compID = static_cast<int>(StatIsComponent(Stat));

  if (compID < 0) {
    return;
  } else {
    StatGetComponentIMD(Stat, compID, &ComponentIMD, &MountIMD);
  }

  glm::mat4 matrix = setMatrix(Position, Rotation, scale);

  /* VTOL bombs are only stats allowed to have NULL ComponentIMD */
  if (StatIsComponent(Stat) != COMPONENT_TYPE::WEAPON
      || (((WeaponStats*)Stat)->weaponSubClass != WEAPON_SUBCLASS::BOMB
          && ((WeaponStats*)Stat)->weaponSubClass != WEAPON_SUBCLASS::EMP)) {
    ASSERT(ComponentIMD, "No ComponentIMD");
  }
  if (!MountIMD) {
    return;
  }

  draw_player_3d_shape(selectedPlayer, MountIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);

  /* translate for weapon mount point */
  if (MountIMD->nconnectors) {
    matrix *= glm::translate(glm::vec3(MountIMD->connectors->xzy()));
  }
  if (ComponentIMD) {
    draw_player_3d_shape(selectedPlayer, ComponentIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
  }
}

// Render a research item given a BASE_STATS structure.
//
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
    draw_player_3d_shape(selectedPlayer, MountIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
  }
  draw_player_3d_shape(selectedPlayer, ResearchIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
}


static inline iIMDShape* getLeftPropulsionIMD(Droid* psDroid)
{
	int bodyStat = psDroid->asBits[COMP_BODY];
	int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + LEFT_PROP];
}

static inline iIMDShape* getRightPropulsionIMD(Droid* psDroid)
{
	int bodyStat = psDroid->asBits[COMP_BODY];
	int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + RIGHT_PROP];
}

void drawMuzzleFlash(const Weapon& sWeap, const iIMDShape* weaponImd, iIMDShape* flashImd,
                     PIELIGHT buildingBrightness, int pieFlag, int iPieData,
                     const glm::mat4& viewMatrix, uint8_t colour)
{
	if (!weaponImd || !flashImd || !weaponImd->nconnectors ||
      graphicsTime < sWeap.time_last_fired) {
		return;
	}

	auto connector_num = 0;

	// which barrel is firing if model have multiple muzzle connectors?
	if (sWeap.shots_fired && (weaponImd->nconnectors > 1))
	{
		// shoot first, draw later - substract one shot to get correct results
		connector_num = (sWeap.shots_fired - 1) % (weaponImd->nconnectors);
	}

	/* Now we need to move to the end of the firing barrel */
	const glm::mat4 modelMatrix = glm::translate(glm::vec3(
          weaponImd->connectors[connector_num].xzy()));

	// assume no clan colours for muzzle effects
	if (flashImd->numFrames == 0 || flashImd->animInterval <= 0)
	{
		// no anim so display one frame for a fixed time
		if (graphicsTime >= sWeap.time_last_fired && graphicsTime < sWeap.time_last_fired + BASE_MUZZLE_FLASH_DURATION)
		{
			pie_Draw3DShape(flashImd, 0, colour, buildingBrightness, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE,
			                viewMatrix * modelMatrix);
		}
	}
	else if (graphicsTime >= sWeap.time_last_fired)
	{
		// animated muzzle
		const int DEFAULT_ANIM_INTERVAL = 17;
		// A lot of PIE files specify 1, which is too small, so set something bigger as a fallback
		int animRate = MAX(flashImd->animInterval, DEFAULT_ANIM_INTERVAL);
		int frame = (graphicsTime - sWeap.time_last_fired) / animRate;
		if (frame < flashImd->numFrames)
		{
			pie_Draw3DShape(flashImd, frame, colour, buildingBrightness, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE,
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
	iIMDShape *psMoveAnim, *psStillAnim;
	int iConnector;
	PropulsionStats* psPropStats;
	int pieFlag, iPieData;
	PIELIGHT brightness;
	unsigned colour;
	std::size_t i = 0;
	bool didDrawSomething = false;

	glm::mat4 modelMatrix(1.f);

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION && psDroid->lastHitWeapon == WEAPON_SUBCLASS::ELECTRONIC && !
		gamePaused())
	{
		colour = getPlayerColour(rand() % MAX_PLAYERS);
	}
	else
	{
		colour = getPlayerColour(psDroid->get_player());
	}

	/* get propulsion stats */
	psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	ASSERT_OR_RETURN(didDrawSomething, psPropStats != nullptr, "invalid propulsion stats pointer");

	//set pieflag for button object or ingame object
	if (bButton)
	{
		pieFlag = pie_BUTTON;
		brightness = WZCOL_WHITE;
	}
	else
	{
		pieFlag = pie_SHADOW;
		brightness = pal_SetBrightness(psDroid->illumination_level);
		// NOTE: Beware of transporters that are offscreen, on a mission!  We should *not* be checking tiles at this point in time!
		if (!isTransporter(psDroid) && !missionIsOffworld())
		{
			Tile* psTile = worldTile(psDroid->pos.x, psDroid->pos.y);
			if (psTile->jammerBits & alliancebits[psDroid->player])
			{
				pieFlag |= pie_ECM;
			}
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMPONENT_TYPE::PROPULSION] == 0)
	{
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		iPieData = 0;
	}

	if (!bButton && psPropStats->propulsionType == PROPULSION_TYPE::PROPELLOR)
	{
		// FIXME: change when adding submarines to the game
		modelMatrix *= glm::translate(glm::vec3(0.f, -world_coord(1) / 2.3f, 0.f));
	}

	iIMDShape* psShapeProp = (leftFirst ? getLeftPropulsionIMD(psDroid) : getRightPropulsionIMD(psDroid));
	if (psShapeProp)
	{
		if (pie_Draw3DShape(psShapeProp, 0, colour, brightness, pieFlag, iPieData, viewMatrix * modelMatrix))
		{
			didDrawSomething = true;
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMP_BODY] == 0)
	{
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		pieFlag &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	/* Get the body graphic now*/
	iIMDShape* psShapeBody = BODY_IMD(psDroid, psDroid->player);
	if (psShapeBody)
	{
		iIMDShape* strImd = psShapeBody;
		if (psDroid->getType() == DROID_TYPE::PERSON)
		{
			modelMatrix *= glm::scale(glm::vec3(.75f)); // FIXME - hideous....!!!!
		}
		if (strImd->objanimpie[psDroid->animationEvent])
		{
			strImd = psShapeBody->objanimpie[psDroid->animationEvent];
		}
		glm::mat4 viewModelMatrix = viewMatrix * modelMatrix;
		while (strImd)
		{
			if (drawShape(psDroid, strImd, colour, brightness, pieFlag, iPieData, viewModelMatrix))
			{
				didDrawSomething = true;
			}
			strImd = strImd->next;
		}
	}

	/* Render animation effects based on movement or lack thereof, if any */
	psMoveAnim = asBodyStats[psDroid->asBits[COMP_BODY]].ppMoveIMDList[psDroid->asBits[COMP_PROPULSION]];
	psStillAnim = asBodyStats[psDroid->asBits[COMP_BODY]].ppStillIMDList[psDroid->asBits[COMP_PROPULSION]];
	glm::mat4 viewModelMatrix = viewMatrix * modelMatrix;
	if (!bButton && psMoveAnim && psDroid->movement.status != MOVEINACTIVE)
	{
		if (pie_Draw3DShape(psMoveAnim, getModularScaledGraphicsTime(psMoveAnim->animInterval, psMoveAnim->numFrames),
		                    colour, brightness, pie_ADDITIVE, 200, viewModelMatrix))
		{
			didDrawSomething = true;
		}
	}
	else if (!bButton && psStillAnim) // standing still
	{
		if (pie_Draw3DShape(psStillAnim,
		                    getModularScaledGraphicsTime(psStillAnim->animInterval, psStillAnim->numFrames), colour,
		                    brightness, 0, 0, viewModelMatrix))
		{
			didDrawSomething = true;
		}
	}

	//don't change the screen coords of an object if drawing it in a button
	if (!bButton)
	{
		/* set up all the screen coords stuff - need to REMOVE FROM THIS LOOP */
		calcScreenCoords(psDroid, viewModelMatrix);
	}

	/* set default components transparent */
	if (psDroid->asWeaps[0].nStat == 0 &&
		psDroid->asBits[COMP_SENSOR] == 0 &&
		psDroid->asBits[COMP_ECM] == 0 &&
		psDroid->asBits[COMP_BRAIN] == 0 &&
		psDroid->asBits[COMP_REPAIRUNIT] == 0 &&
		psDroid->asBits[COMP_CONSTRUCT] == 0)
	{
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		pieFlag &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	if (psShapeBody && psShapeBody->nconnectors)
	{
		/* vtol weapons attach to connector 2 (underneath);
		 * all others to connector 1 */
		/* VTOL's now skip the first 5 connectors(0 to 4),
		VTOL's use 5,6,7,8 etc now */
		if (psPropStats->propulsionType == PROPULSION_TYPE::LIFT &&
    psDroid->getType() == DROID_TYPE::WEAPON)
		{
			iConnector = VTOL_CONNECTOR_START;
		}
		else
		{
			iConnector = 0;
		}

		switch (psDroid->getType())
		{
		case DROID_DEFAULT:
		case DROID_TRANSPORTER:
		case DROID_SUPERTRANSPORTER:
		case DROID_CYBORG:
		case DROID_CYBORG_SUPER:
		case DROID_WEAPON:
		case DROID_COMMAND: // command droids have a weapon to store all the graphics
			/*	Get the mounting graphic - we've already moved to the right position
			Allegedly - all droids will have a mount graphic so this shouldn't
			fall on it's arse......*/
			/* Double check that the weapon droid actually has any */
			for (i = 0; i < psDroid->numWeaps; i++)
			{
				if ((psDroid->asWeaps[i].nStat > 0 || psDroid->type == DROID_DEFAULT)
					&& psShapeBody->connectors)
				{
					Rotation rot = getInterpolatedWeaponRotation(psDroid, i, graphicsTime);

					glm::mat4 localModelMatrix = modelMatrix;

					//to skip number of VTOL_CONNECTOR_START ground unit connectors
					if (iConnector < VTOL_CONNECTOR_START)
					{
						localModelMatrix *= glm::translate(glm::vec3(psShapeBody->connectors[i].xzy()));
					}
					else
					{
						localModelMatrix *= glm::translate(glm::vec3(psShapeBody->connectors[iConnector + i].xzy()));
					}
					localModelMatrix *= glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));

					/* vtol weapons inverted */
					if (iConnector >= VTOL_CONNECTOR_START)
					{
						//this might affect gun rotation
						localModelMatrix *= glm::rotate(UNDEG(65536 / 2), glm::vec3(0.f, 0.f, 1.f));
					}

					/* Get the mount graphic */
					iIMDShape* psShape = WEAPON_MOUNT_IMD(psDroid, i);

					int recoilValue = getRecoil(psDroid->asWeaps[i]);
					localModelMatrix *= glm::translate(glm::vec3(0.f, 0.f, recoilValue / 3.f));

					/* Draw it */
					if (psShape)
					{
						if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData,
						                    viewMatrix * localModelMatrix))
						{
							didDrawSomething = true;
						}
					}
					localModelMatrix *= glm::translate(glm::vec3(0, 0, recoilValue));

					/* translate for weapon mount point */
					if (psShape && psShape->nconnectors)
					{
						localModelMatrix *= glm::translate(glm::vec3(psShape->connectors->xzy()));
					}

					/* vtol weapons inverted */
					if (iConnector >= VTOL_CONNECTOR_START)
					{
						//pitch the barrel down
						localModelMatrix *= glm::rotate(UNDEG(-rot.pitch), glm::vec3(1.f, 0.f, 0.f));
					}
					else
					{
						//pitch the barrel up
						localModelMatrix *= glm::rotate(UNDEG(rot.pitch), glm::vec3(1.f, 0.f, 0.f));
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
						drawMuzzleFlash(psDroid->asWeaps[i], psShape, MUZZLE_FLASH_PIE(psDroid, i), brightness, pieFlag,
						                iPieData, localViewModelMatrix);
					}
				}
			}
			break;

		case DROID_SENSOR:
		case DROID_CONSTRUCT:
		case DROID_CYBORG_CONSTRUCT:
		case DROID_ECM:
		case DROID_REPAIR:
		case DROID_CYBORG_REPAIR:
			{
				Rotation rot = getInterpolatedWeaponRotation(psDroid, 0, graphicsTime);
				iIMDShape* psShape = nullptr;
				iIMDShape* psMountShape = nullptr;

				switch (psDroid->type)
				{
				default:
					ASSERT(false, "Bad component type");
					break;
				case DROID_SENSOR:
					psMountShape = SENSOR_MOUNT_IMD(psDroid, psDroid->player);
				/* Get the sensor graphic, assuming it's there */
					psShape = SENSOR_IMD(psDroid, psDroid->player);
					break;
				case DROID_CONSTRUCT:
				case DROID_CYBORG_CONSTRUCT:
					psMountShape = CONSTRUCT_MOUNT_IMD(psDroid, psDroid->player);
				/* Get the construct graphic assuming it's there */
					psShape = CONSTRUCT_IMD(psDroid, psDroid->player);
					break;
				case DROID_ECM:
					psMountShape = ECM_MOUNT_IMD(psDroid, psDroid->player);
				/* Get the ECM graphic assuming it's there.... */
					psShape = ECM_IMD(psDroid, psDroid->player);
					break;
				case DROID_REPAIR:
				case DROID_CYBORG_REPAIR:
					psMountShape = REPAIR_MOUNT_IMD(psDroid, psDroid->player);
				/* Get the Repair graphic assuming it's there.... */
					psShape = REPAIR_IMD(psDroid, psDroid->player);
					break;
				}
				/*	Get the mounting graphic - we've already moved to the right position
				Allegedly - all droids will have a mount graphic so this shouldn't
				fall on it's arse......*/
				//sensor and cyborg and ecm uses connectors[0]

				glm::mat4 localModelMatrix = modelMatrix;
				/* vtol weapons inverted */
				if (iConnector >= VTOL_CONNECTOR_START)
				{
					//this might affect gun rotation
					localModelMatrix *= glm::rotate(UNDEG(65536 / 2), glm::vec3(0.f, 0.f, 1.f));
				}

				localModelMatrix *= glm::translate(glm::vec3(psShapeBody->connectors[0].xzy()));

				localModelMatrix *= glm::rotate(UNDEG(-rot.direction), glm::vec3(0.f, 1.f, 0.f));
				/* Draw it */
				if (psMountShape)
				{
					if (pie_Draw3DShape(psMountShape, 0, colour, brightness, pieFlag, iPieData,
					                    viewMatrix * localModelMatrix))
					{
						didDrawSomething = true;
					}
				}

				/* translate for construct mount point if cyborg */
				if (cyborgDroid(psDroid) && psMountShape && psMountShape->nconnectors)
				{
					localModelMatrix *= glm::translate(glm::vec3(psMountShape->connectors[0].xzy()));
				}

				/* Draw it */
				if (psShape)
				{
					if (pie_Draw3DShape(psShape, 0, colour, brightness, pieFlag, iPieData,
					                    viewMatrix * localModelMatrix))
					{
						didDrawSomething = true;
					}

					// In repair droid case only:
					if ((psDroid->getType() == DROID_TYPE::REPAIRER || 
               psDroid->getType() == DROID_TYPE::CYBORG_REPAIR) &&
							psShape->nconnectors && psDroid->getAction() == ACTION::DROID_REPAIR)
					{
						Spacetime st = interpolateObjectSpacetime(psDroid, graphicsTime);
						localModelMatrix *= glm::translate(glm::vec3(psShape->connectors[0].xzy()));
						localModelMatrix *= glm::translate(glm::vec3(0.f, -20.f, 0.f));

						psShape = getImdFromIndex(MI_FLAME);

						/* Rotate for droid */
						localModelMatrix *= glm::rotate(UNDEG(st.rot.direction), glm::vec3(0.f, 1.f, 0.f));
						localModelMatrix *= glm::rotate(UNDEG(-st.rot.pitch), glm::vec3(1.f, 0.f, 0.f));
						localModelMatrix *= glm::rotate(UNDEG(-st.rot.roll), glm::vec3(0.f, 0.f, 1.f));
						//rotate Y
						localModelMatrix *= glm::rotate(UNDEG(rot.direction), glm::vec3(0.f, 1.f, 0.f));

						localModelMatrix *= glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f));
						localModelMatrix *= glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(1.f, 0.f, 0.f));

						if (pie_Draw3DShape(
							psShape, getModularScaledGraphicsTime(psShape->animInterval, psShape->numFrames), 0,
							brightness, pie_ADDITIVE, 140, viewMatrix * localModelMatrix))
						{
							didDrawSomething = true;
						}

						//						localModelMatrix *= glm::rotate(UNDEG(playerPos.r.x), glm::vec3(1.f, 0.f, 0.f)); // Not used?
						//						localModelMatrix *= glm::rotate(UNDEG(playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)); // Not used?
					}
				}
				break;
			}
		case DROID_PERSON:
			// no extra mounts for people
			break;
		default:
			ASSERT(!"invalid droid type", "Whoa! Weirdy type of droid found in drawComponentObject!!!");
			break;
		}
	}

	/* set default components transparent */
	if (psDroid->asBits[COMP_PROPULSION] == 0)
	{
		pieFlag |= pie_TRANSLUCENT;
		iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
	}
	else
	{
		pieFlag &= ~pie_TRANSLUCENT;
		iPieData = 0;
	}

	// now render the other propulsion side
	psShapeProp = (leftFirst ? getRightPropulsionIMD(psDroid) : getLeftPropulsionIMD(psDroid));
	if (psShapeProp)
	{
		if (pie_Draw3DShape(psShapeProp, 0, colour, brightness, pieFlag, iPieData, viewModelMatrix))
		// Safe to use viewModelMatrix because modelView has not been changed since it was calculated
		{
			didDrawSomething = true;
		}
	}

	return didDrawSomething;
}


// Render a composite droid given a DROID_TEMPLATE structure.
//
void displayComponentButtonTemplate(DroidTemplate* psTemplate, const Vector3i* Rotation, const Vector3i* Position,
                                    int scale)
{
	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// Decide how to sort it.
	leftFirst = angleDelta(DEG(Rotation->y)) < 0;

	Droid Droid(0, selectedPlayer);
	memset(Droid.asBits, 0, sizeof(Droid.asBits));
	droidSetBits(psTemplate, &Droid);

	Droid.pos = Vector3i(0, 0, 0);
	Droid.rot = Vector3i(0, 0, 0);

	//draw multi component object as a button object
	displayCompObj(&Droid, true, matrix);
}


// Render a composite droid given a DROID structure.
//
void displayComponentButtonObject(Droid* psDroid, const Vector3i* Rotation, const Vector3i* Position, int scale)
{
	SDWORD difference;

	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// Decide how to sort it.
	difference = Rotation->y % 360;

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

	leftFirst = angleDelta(playerPos.r.y - st.rot.direction) <= 0;

	/* Get the real position */
	position.x = st.pos.x - playerPos.p.x;
	position.z = -(st.pos.y - playerPos.p.z);
	position.y = st.pos.z;

	if (isTransporter(*psDroid))
	{
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

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION && psDroid->lastHitWeapon == WEAPON_SUBCLASS::ELECTRONIC)
	{
		modelMatrix *= objectShimmy((SimpleObject*)psDroid);
	}

	// now check if the projected circle is within the screen boundaries
	if (!clipDroidOnScreen(psDroid, viewMatrix * modelMatrix))
	{
		return;
	}

	if (psDroid->lastHitWeapon == WEAPON_SUBCLASS::EMP && graphicsTime - psDroid->timeLastHit < EMP_DISABLE_TIME)
	{
		Vector3i effectPosition;

		//add an effect on the droid
		effectPosition.x = st.position.x + DROID_EMP_SPREAD;
		effectPosition.y = st.position.z + rand() % 8;
		effectPosition.z = st.position.y + DROID_EMP_SPREAD;
		effectGiveAuxVar(90 + rand() % 20);
		addEffect(&effectPosition, EFFECT_EXPLOSION, EXPLOSION_TYPE_PLASMA, false, nullptr, 0);
	}

	if (psDroid->visibleForLocalDisplay() == UBYTE_MAX)
	{
		//ingame not button object
		//should render 3 mounted weapons now
		if (displayCompObj(psDroid, false, viewMatrix * modelMatrix))
		{
			// did draw something to the screen - update the framenumber
			psDroid->sDisplay.frame_number = frameGetFrameNumber();
		}
	}
	else
	{
		auto frame = graphicsTime / BLIP_ANIM_DURATION + psDroid->get_id() % 8192;
		// de-sync the blip effect, but don't overflow the int
		if (pie_Draw3DShape(getImdFromIndex(MI_BLIP), frame, 0, WZCOL_WHITE, pie_ADDITIVE,
		                    psDroid->visibleForLocalDisplay() / 2, viewMatrix * modelMatrix))
		{
			psDroid->sDisplay.frame_number = frameGetFrameNumber();
		}
	}
}


void destroyFXDroid(Droid* psDroid, unsigned impactTime)
{
	for (int i = 0; i < 5; ++i)
	{
		iIMDShape* psImd = nullptr;

		int maxHorizontalScatter = TILE_UNITS / 4;
		int heightScatter = TILE_UNITS / 5;
		Vector2i horizontalScatter = iSinCosR(rand(), rand() % maxHorizontalScatter);

		Vector3i pos = (psDroid->get_position() + Vector3i(horizontalScatter, 16 + heightScatter)).xzy();
		switch (i) {
		case 0:
			switch (psDroid->getType())
			{
			case DROID_DEFAULT:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_WEAPON:
			case DROID_COMMAND:
				if (psDroid->numWeaps > 0)
				{
					if (psDroid->asWeaps[0].nStat > 0)
					{
						psImd = WEAPON_MOUNT_IMD(psDroid, 0);
					}
				}
				break;
			default:
				break;
			}
			break;
		case 1:
			switch (psDroid->type)
			{
			case DROID_DEFAULT:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_WEAPON:
			case DROID_COMMAND:
				if (psDroid->numWeaps)
				{
					// get main weapon
					psImd = WEAPON_IMD(psDroid, 0);
				}
				break;
			default:
				break;
			}
			break;
		}
		if (psImd == nullptr)
		{
			psImd = getRandomDebrisImd();
		}
		// Tell the effect system that it needs to use this player's color for the next effect
		SetEffectForPlayer(psDroid->player);
		addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR, true, psImd, getPlayerColour(psDroid->player),
		          impactTime);
	}
}


void compPersonToBits(Droid* psDroid)
{
	Vector3i position; //,rotation,velocity;
	iIMDShape *headImd, *legsImd, *armImd, *bodyImd;
	unsigned col;

	if (!psDroid->visibleForLocalDisplay()) // display only - should not affect game state
	{
		/* We can't see the person or cyborg - so get out */
		return;
	}
	/* get bits pointers according to whether baba or cyborg*/
	if (cyborgDroid(psDroid))
	{
		// This is probably unused now, since there's a more appropriate effect for cyborgs.
		headImd = getImdFromIndex(MI_CYBORG_HEAD);
		legsImd = getImdFromIndex(MI_CYBORG_LEGS);
		armImd = getImdFromIndex(MI_CYBORG_ARM);
		bodyImd = getImdFromIndex(MI_CYBORG_BODY);
	}
	else
	{
		headImd = getImdFromIndex(MI_BABA_HEAD);
		legsImd = getImdFromIndex(MI_BABA_LEGS);
		armImd = getImdFromIndex(MI_BABA_ARM);
		bodyImd = getImdFromIndex(MI_BABA_BODY);
	}

	/* Get where he's at */
	position.x = psDroid->pos.x;
	position.y = psDroid->pos.z + 1;
	position.z = psDroid->pos.y;

	/* Tell about player colour */
	col = getPlayerColour(psDroid->get_player());

	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, headImd, col, gameTime - deltaGameTime + 1);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, legsImd, col, gameTime - deltaGameTime + 1);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, armImd, col, gameTime - deltaGameTime + 1);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, bodyImd, col, gameTime - deltaGameTime + 1);
}


SDWORD rescaleButtonObject(SDWORD radius, SDWORD baseScale, SDWORD baseRadius)
{
	SDWORD newScale;
	newScale = 100 * baseRadius;
	newScale /= radius;
	if (baseScale > 0)
	{
		newScale += baseScale;
		newScale /= 2;
	}
	return newScale;
}
