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
 * @file component.c
 * Draws component objects - oh yes indeed.
*/

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/netplay/netplay.h"

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
#include <glm/gtx/transform.hpp>

#define GetRadius(x) ((x)->sradius)

#define	DEFAULT_COMPONENT_TRANSLUCENCY	128
#define	DROID_EMP_SPREAD	(20 - rand()%40)

//VTOL weapon connector start
#define VTOL_CONNECTOR_START 5

static bool		leftFirst;

// Colour Lookups
// use col = MAX_PLAYERS for anycolour (see multiint.c)
bool setPlayerColour(UDWORD player, UDWORD col)
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

UBYTE getPlayerColour(UDWORD pl)
{
	if (pl == MAX_PLAYERS)
	{
		return 0; // baba
	}
	ASSERT_OR_RETURN(0, pl < MAX_PLAYERS, "Invalid player number %u", pl);
	return NetPlay.players[pl].colour;
}

static glm::mat4 setMatrix(const Vector3i *Position, const Vector3i *Rotation, int scale)
{
	return glm::translate(glm::vec3(*Position)) *
		glm::rotate(UNDEG(DEG(Rotation->x)), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->y)), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(DEG(Rotation->z)), glm::vec3(0.f, 0.f, 1.f)) *
		glm::scale(glm::vec3(scale / 100.f));
}

UDWORD getComponentDroidRadius(Droid *)
{
	return 100;
}


UDWORD getComponentDroidTemplateRadius(DroidStats *)
{
	return 100;
}


UDWORD getComponentRadius(StatsObject *psComponent)
{
	iIMDShape *ComponentIMD = nullptr;
	iIMDShape *MountIMD = nullptr;
	SDWORD compID;

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
	    || (((WEAPON_STATS *)psComponent)->weaponSubClass != WSC_BOMB
	        && ((WEAPON_STATS *)psComponent)->weaponSubClass != WSC_EMP))
	{
		ASSERT(ComponentIMD, "No ComponentIMD!");
	}

	return COMPONENT_RADIUS;
}


UDWORD getResearchRadius(StatsObject *Stat)
{
	iIMDShape *ResearchIMD = ((RESEARCH *)Stat)->pIMD;

	if (ResearchIMD)
	{
		return GetRadius(ResearchIMD);
	}

	debug(LOG_ERROR, "ResearchPIE == NULL");

	return 100;
}


UDWORD getStructureSizeMax(Structure *psStructure)
{
	//radius based on base plate size
	return MAX(psStructure->stats->baseWidth, psStructure->stats->baseBreadth);
}

UDWORD getStructureStatSizeMax(StructureStats *Stats)
{
	//radius based on base plate size
	return MAX(Stats->baseWidth, Stats->baseBreadth);
}

UDWORD getStructureStatHeight(StructureStats *psStat)
{
	if (psStat->pIMD[0])
	{
		return (psStat->pIMD[0]->max.y - psStat->pIMD[0]->min.y);
	}

	return 0;
}

static void draw_player_3d_shape(uint32_t player_index, iIMDShape *shape, int frame, PIELIGHT colour, int pie_flag, int pie_flag_data, const glm::mat4 &model_view)
{
	auto faction_shape = getFactionIMD(getPlayerFaction(player_index), shape);
	pie_Draw3DShape(faction_shape, frame, getPlayerColour(player_index), colour, pie_flag, pie_flag_data, model_view);
}

void displayIMDButton(iIMDShape *IMDShape, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	draw_player_3d_shape(selectedPlayer, IMDShape, 0, WZCOL_WHITE, pie_BUTTON, 0, setMatrix(Position, Rotation, scale));
}

static void sharedStructureButton(StructureStats *Stats, iIMDShape *strImd, const Vector3i *Rotation, const Vector3i *Position, int scale)
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
	baseImd = Stats->pBaseIMD;

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
			weaponImd[i] = nullptr;//weapon is gun ecm or sensor
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
				if (Stats->pECM != nullptr)
				{
					weaponImd[i] =  Stats->pECM->pIMD;
					mountImd[i] =  Stats->pECM->pMountGraphic;
				}
			}

			if (weaponImd[i] == nullptr)
			{
				//check for sensor
				if (Stats->pSensor != nullptr)
				{
					weaponImd[i] =  Stats->pSensor->pIMD;
					mountImd[i]  =  Stats->pSensor->pMountGraphic;
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
					draw_player_3d_shape(selectedPlayer, mountImd[i], 0, WZCOL_WHITE, pie_BUTTON, 0, matrix * localMatrix);
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

void displayStructureButton(Structure *psStructure, const Vector3i *rotation, const Vector3i *Position, int scale)
{
	sharedStructureButton(psStructure->stats, psStructure->displayData.imd, rotation, Position, scale);
}

void displayStructureStatButton(StructureStats *Stats, const Vector3i *rotation, const Vector3i *Position, int scale)
{
	sharedStructureButton(Stats, Stats->pIMD[0], rotation, Position, scale);
}

// Render a component given a BASE_STATS structure.
//
void displayComponentButton(StatsObject *Stat, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	iIMDShape *ComponentIMD = nullptr;
	iIMDShape *MountIMD = nullptr;
	int compID = StatIsComponent(Stat);

	if (compID >= 0)
	{
		StatGetComponentIMD(Stat, compID, &ComponentIMD, &MountIMD);
	}
	else
	{
		return;
	}
	glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	/* VTOL bombs are only stats allowed to have NULL ComponentIMD */
	if (StatIsComponent(Stat) != COMP_WEAPON
	    || (((WEAPON_STATS *)Stat)->weaponSubClass != WSC_BOMB
	        && ((WEAPON_STATS *)Stat)->weaponSubClass != WSC_EMP))
	{
		ASSERT(ComponentIMD, "No ComponentIMD");
	}

	if (MountIMD)
	{
		draw_player_3d_shape(selectedPlayer, MountIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);

		/* translate for weapon mount point */
		if (MountIMD->nconnectors)
		{
			matrix *= glm::translate(glm::vec3(MountIMD->connectors->xzy()));
		}
	}
	if (ComponentIMD)
	{
		draw_player_3d_shape(selectedPlayer, ComponentIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
	}
}


// Render a research item given a BASE_STATS structure.
//
void displayResearchButton(StatsObject *Stat, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	iIMDShape *ResearchIMD = ((RESEARCH *)Stat)->pIMD;
	iIMDShape *MountIMD = ((RESEARCH *)Stat)->pIMD2;

	ASSERT(ResearchIMD, "ResearchIMD is NULL");
	if (ResearchIMD)
	{
		const glm::mat4 &matrix = setMatrix(Position, Rotation, scale);

		if (MountIMD)
		{
			draw_player_3d_shape(selectedPlayer, MountIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
		}
		draw_player_3d_shape(selectedPlayer, ResearchIMD, 0, WZCOL_WHITE, pie_BUTTON, 0, matrix);
	}
}


static inline iIMDShape *getLeftPropulsionIMD(Droid *psDroid)
{
	int bodyStat = psDroid->asBits[COMP_BODY];
	int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + LEFT_PROP];
}

static inline iIMDShape *getRightPropulsionIMD(Droid *psDroid)
{
	int bodyStat = psDroid->asBits[COMP_BODY];
	int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + RIGHT_PROP];
}

void drawMuzzleFlash(Weapon sWeap, iIMDShape *weaponImd, iIMDShape *flashImd, PIELIGHT buildingBrightness, int pieFlag, int iPieData, const glm::mat4 &viewMatrix, UBYTE colour)
{
	if (!weaponImd || !flashImd || !weaponImd->nconnectors || graphicsTime < sWeap.lastFired)
	{
		return;
	}

	int connector_num = 0;

	// which barrel is firing if model have multiple muzzle connectors?
	if (sWeap.shotsFired && (weaponImd->nconnectors > 1))
	{
		// shoot first, draw later - substract one shot to get correct results
		connector_num = (sWeap.shotsFired - 1) % (weaponImd->nconnectors);
	}

	/* Now we need to move to the end of the firing barrel */
	const glm::mat4 modelMatrix = glm::translate(glm::vec3(weaponImd->connectors[connector_num].xzy()));

	// assume no clan colours for muzzle effects
	if (flashImd->numFrames == 0 || flashImd->animInterval <= 0)
	{
		// no anim so display one frame for a fixed time
		if (graphicsTime >= sWeap.lastFired && graphicsTime < sWeap.lastFired + BASE_MUZZLE_FLASH_DURATION)
		{
			pie_Draw3DShape(flashImd, 0, colour, buildingBrightness, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE, viewMatrix * modelMatrix);
		}
	}
	else if (graphicsTime >= sWeap.lastFired)
	{
		// animated muzzle
		const int DEFAULT_ANIM_INTERVAL = 17; // A lot of PIE files specify 1, which is too small, so set something bigger as a fallback
		int animRate = MAX(flashImd->animInterval, DEFAULT_ANIM_INTERVAL);
		int frame = (graphicsTime - sWeap.lastFired) / animRate;
		if (frame < flashImd->numFrames)
		{
			pie_Draw3DShape(flashImd, frame, colour, buildingBrightness, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE, viewMatrix * modelMatrix);
		}
	}
}

// Render a composite droid given a DROID_TEMPLATE structure.
//
void displayComponentButtonTemplate(DroidStats *psTemplate, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	const glm::mat4 matrix = setMatrix(Position, Rotation, scale);

	// Decide how to sort it.
	leftFirst = angleDelta(DEG(Rotation->y)) < 0;

        Droid Droid(0, selectedPlayer);
	memset(Droid.asBits, 0, sizeof(Droid.asBits));
	droidSetBits(psTemplate, &Droid);

	Droid.position = Vector3i(0, 0, 0);
	Droid.rotation = Vector3i(0, 0, 0);

	//draw multi component object as a button object
	displayCompObj(&Droid, true, matrix);
}


// Render a composite droid given a DROID structure.
//
void displayComponentButtonObject(Droid *psDroid, const Vector3i *Rotation, const Vector3i *Position, int scale)
{
	SDWORD		difference;

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
void displayComponentObject(Droid *psDroid, const glm::mat4 &viewMatrix)
{
	Vector3i position, rotation;
	Spacetime st = interpolateObjectSpacetime(psDroid, graphicsTime);

	leftFirst = angleDelta(playerPos.r.y - st.m_rotation.direction) <= 0;

	/* Get the real position */
	position.x = st.m_position.x - playerPos.p.x;
	position.z = -(st.m_position.y - playerPos.p.z);
	position.y = st.m_position.z;

	if (isTransporter(psDroid))
	{
		position.y += bobTransporterHeight();
	}

	/* Get all the pitch,roll,yaw info */
	rotation.y = -st.m_rotation.direction;
	rotation.x = st.m_rotation.pitch;
	rotation.z = st.m_rotation.roll;

	/* Translate origin */
	/* Rotate for droid */
	glm::mat4 modelMatrix = glm::translate(glm::vec3(position)) *
		glm::rotate(UNDEG(rotation.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(rotation.x), glm::vec3(1.f, 0.f, 0.f)) *
		glm::rotate(UNDEG(rotation.z), glm::vec3(0.f, 0.f, 1.f));

	if (psDroid->timeLastHit - graphicsTime < ELEC_DAMAGE_DURATION && psDroid->lastHitWeapon == WSC_ELECTRONIC)
	{
		modelMatrix *= objectShimmy((GameObject *) psDroid);
	}

	// now check if the projected circle is within the screen boundaries
	if(!clipDroidOnScreen(psDroid, viewMatrix * modelMatrix))
	{
		return;
	}

	if (psDroid->lastHitWeapon == WSC_EMP && graphicsTime - psDroid->timeLastHit < EMP_DISABLE_TIME)
	{
		Vector3i effectPosition;

		//add an effect on the droid
		effectPosition.x = st.m_position.x + DROID_EMP_SPREAD;
		effectPosition.y = st.m_position.z + rand() % 8;
		effectPosition.z = st.m_position.y + DROID_EMP_SPREAD;
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
			psDroid->displayData.frameNumber = frameGetFrameNumber();
		}
	}
	else
	{
		int frame = graphicsTime / BLIP_ANIM_DURATION + psDroid->id % 8192; // de-sync the blip effect, but don't overflow the int
		if (pie_Draw3DShape(getImdFromIndex(MI_BLIP), frame, 0, WZCOL_WHITE, pie_ADDITIVE, psDroid->visibleForLocalDisplay() / 2, viewMatrix * modelMatrix))
		{
			psDroid->displayData.frameNumber = frameGetFrameNumber();
		}
	}
}


void destroyFXDroid(Droid *psDroid, unsigned impactTime)
{
	for (int i = 0; i < 5; ++i)
	{
		iIMDShape *psImd = nullptr;

		int maxHorizontalScatter = TILE_UNITS / 4;
		int heightScatter = TILE_UNITS / 5;
		Vector2i horizontalScatter = iSinCosR(rand(), rand() % maxHorizontalScatter);

		Vector3i pos = (psDroid->position + Vector3i(horizontalScatter, 16 + heightScatter)).xzy();
		switch (i)
		{
		case 0:
			switch (psDroid->droidType)
			{
			case DROID_DEFAULT:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_WEAPON:
			case DROID_COMMAND:
				if (psDroid->numWeapons > 0)
				{
					if (psDroid->weaponList[0].nStat > 0)
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
			switch (psDroid->droidType)
			{
			case DROID_DEFAULT:
			case DROID_CYBORG:
			case DROID_CYBORG_SUPER:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_WEAPON:
			case DROID_COMMAND:
				if (psDroid->numWeapons)
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
		SetEffectForPlayer(psDroid->owningPlayer);
		addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR, true, psImd, getPlayerColour(psDroid->owningPlayer), impactTime);
	}
}


void	compPersonToBits(Droid *psDroid)
{
	Vector3i position;	//,rotation,velocity;
	iIMDShape	*headImd, *legsImd, *armImd, *bodyImd;
	UDWORD		col;

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
		armImd  = getImdFromIndex(MI_CYBORG_ARM);
		bodyImd = getImdFromIndex(MI_CYBORG_BODY);
	}
	else
	{
		headImd = getImdFromIndex(MI_BABA_HEAD);
		legsImd = getImdFromIndex(MI_BABA_LEGS);
		armImd  = getImdFromIndex(MI_BABA_ARM);
		bodyImd = getImdFromIndex(MI_BABA_BODY);
	}

	/* Get where he's at */
	position.x = psDroid->position.x;
	position.y = psDroid->position.z + 1;
	position.z = psDroid->position.y;

	/* Tell about player colour */
	col = getPlayerColour(psDroid->owningPlayer);

	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, headImd, col, gameTime - deltaGameTime + 1);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, legsImd, col, gameTime - deltaGameTime + 1);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, armImd, col, gameTime - deltaGameTime + 1);
	addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET, true, bodyImd, col, gameTime - deltaGameTime + 1);
}


SDWORD	rescaleButtonObject(SDWORD radius, SDWORD baseScale, SDWORD baseRadius)
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