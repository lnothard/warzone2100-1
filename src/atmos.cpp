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
 * @file atmos.cpp
 * Handles atmospherics such as snow and rain
*/

#include <glm/gtx/transform.hpp>
#include "lib/ivis_opengl/piepalette.h"
#include "wzmaplib/map.h"

#include "atmos.h"
#include "display3d.h"
#include "effects.h"
#include "miscimd.h"
#include "display.h"

int mapWidth, mapHeight;
int map_Height(int, int);
bool gamePaused();


/* Roughly one per tile */
static constexpr auto	MAX_ATMOS_PARTICLES	= MAP_MAXWIDTH * MAP_MAXHEIGHT;
static const auto	SNOW_SPEED_DRIFT = (40 - rand() % 80);
static const auto SNOW_SPEED_FALL  = (0 - (rand() % 40 + 80));
static const auto	RAIN_SPEED_DRIFT = (rand() % 50);
static const auto	RAIN_SPEED_FALL  = (0 - ((rand() % 300) + 700));

static std::vector<Particle> asAtmosParts;
static unsigned freeParticle;
static WEATHER_TYPE weather = WEATHER_TYPE::NONE;

/* Setup all the particles */
void atmosInitSystem()
{
	if (asAtmosParts.empty() &&
      weather != WEATHER_TYPE::NONE) {
		asAtmosParts.resize(MAX_ATMOS_PARTICLES);
	}
  freeParticle = 0;
}

/*	Makes a particle wrap around - if it goes off the grid, then it returns
	on the other side - provided it's still on world... Which it should be */
static void testParticleWrap(Particle* psPart)
{
	/* Gone off left side */
	if (psPart->position.x < playerPos.p.x - world_coord(visibleTiles.x) / 2) {
		psPart->position.x += world_coord(visibleTiles.x);
	}

	/* Gone off right side */
	else if (psPart->position.x > (playerPos.p.x + world_coord(visibleTiles.x) / 2)) {
		psPart->position.x -= world_coord(visibleTiles.x);
	}

	/* Gone off top */
	if (psPart->position.z < playerPos.p.z - world_coord(visibleTiles.y) / 2) {
		psPart->position.z += world_coord(visibleTiles.y);
	}

	/* Gone off bottom */
	else if (psPart->position.z > (playerPos.p.z + world_coord(visibleTiles.y) / 2)) {
		psPart->position.z -= world_coord(visibleTiles.y);
	}
}

/* Moves one of the particles */
static void processParticle(Particle* psPart)
{
	/* Only move if the game isn't paused */
  if (gamePaused()) return;

  /* Move the particle - frame rate controlled */
  psPart->position.x += graphicsTimeAdjustedIncrement(psPart->velocity.x);
  psPart->position.y += graphicsTimeAdjustedIncrement(psPart->velocity.y);
  psPart->position.z += graphicsTimeAdjustedIncrement(psPart->velocity.z);

  /* Wrap it around if it's gone off grid... */
  testParticleWrap(psPart);

  /* If it's gone off the WORLD... */
  if (psPart->position.x < 0 || psPart->position.z < 0 ||
      psPart->position.x > ((mapWidth - 1) * TILE_UNITS) ||
      psPart->position.z > ((mapHeight - 1) * TILE_UNITS)) {
    // then kill it
    psPart->status = PARTICLE_STATUS::INACTIVE;
    return;
  }

  /* What height is the ground under it? Only do if low enough...*/
  if (psPart->position.y < TILE_MAX_HEIGHT) {
    /* Get ground height */
    auto groundHeight = map_Height(static_cast<int>(psPart->position.x),
                                   static_cast<int>(psPart->position.z));

    /* Are we below ground? */
    if (psPart->position.y < groundHeight || psPart->position.y < 0.f) {
      /* Kill it and return */
      psPart->status = PARTICLE_STATUS::INACTIVE;

      if (psPart->type != PARTICLE_TYPE::RAIN)
        return;

      auto x = map_coord(static_cast<int>(psPart->position.x));
      auto y = map_coord(static_cast<int>(psPart->position.z));
      auto psTile = mapTile(x, y);
      if (terrainType(psTile) == TER_WATER &&
          TEST_TILE_VISIBLE_TO_SELECTEDPLAYER(psTile)) {
        // display-only check for adding effect
        auto pos = Position{psPart->position.x, groundHeight, psPart->position.z};
        effectSetSize(60);
        addEffect(&pos, EFFECT_GROUP::EXPLOSION,
                  EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED,
                  true, getImdFromIndex(MI_SPLASH), 0);
      }
      return;
    }
  }
  if (psPart->type == PARTICLE_TYPE::SNOW) {
    if (rand() % 30 == 1) {
      psPart->velocity.z = (float)SNOW_SPEED_DRIFT;
    }
    if (rand() % 30 == 1) {
      psPart->velocity.x = (float)SNOW_SPEED_DRIFT;
    }
  }
}

/* Adds a particle to the system if it can */
static void atmosAddParticle(const Vector3f& pos, PARTICLE_TYPE type)
{
	unsigned activeCount;
  int i;

	for (i = freeParticle, activeCount = 0; asAtmosParts[i].status == PARTICLE_STATUS::ACTIVE && activeCount < MAX_ATMOS_PARTICLES; i++) {
		activeCount++;
		/* Check for wrap around */
		if (i >= (MAX_ATMOS_PARTICLES - 1)) {
			/* Go back to the first one */
			i = 0;
		}
	}

	/* Check the list isn't just full of essential effects */
	if (activeCount >= MAX_ATMOS_PARTICLES - 1) {
		/* All of the particles active!?!? */
		return;
	}
	else {
		freeParticle = i;
	}

	/* Record it's type */
	asAtmosParts[freeParticle].type = type;

	/* Make it active */
	asAtmosParts[freeParticle].status = PARTICLE_STATUS::ACTIVE;

	/* Setup the imd */
	switch (type) {
	  case PARTICLE_TYPE::SNOW:
	  	asAtmosParts[freeParticle].imd = std::make_unique<iIMDShape>(*getImdFromIndex(MI_SNOW));
	  	asAtmosParts[freeParticle].size = 80;
	  	break;
      case PARTICLE_TYPE::RAIN:
	  	asAtmosParts[freeParticle].imd = std::make_unique<iIMDShape>(*getImdFromIndex(MI_RAIN));
	  	asAtmosParts[freeParticle].size = 50;
	  	break;
	}

	/* Setup position */
	asAtmosParts[freeParticle].position = pos;

	/* Setup its velocity */
	if (type == PARTICLE_TYPE::RAIN) {
		asAtmosParts[freeParticle].velocity = Vector3f(RAIN_SPEED_DRIFT, RAIN_SPEED_FALL, RAIN_SPEED_DRIFT);
	}
	else {
		asAtmosParts[freeParticle].velocity = Vector3f(SNOW_SPEED_DRIFT, SNOW_SPEED_FALL, SNOW_SPEED_DRIFT);
	}
}

void atmosUpdateSystem()
{
	Vector3f pos;

	// we don't want to do any of this while paused.
  if (gamePaused() || weather == WEATHER_TYPE::NONE)
    return;

  for (auto i = 0; i < MAX_ATMOS_PARTICLES; i++)
  {
    /* See if it's active */
    if (asAtmosParts[i].status == PARTICLE_STATUS::ACTIVE) {
      processParticle(&asAtmosParts[i]);
    }
  }

  // The original code added a fixed number of particles per tick. To take into account game speed
  // we have to accumulate a fractional number of particles to add them at a slower or faster rate.
  static double accumulatedParticlesToAdd = 0.0;

  double gameTimeModVal = gameTimeGetMod().asDouble();
  if (!std::isnan(gameTimeModVal)) {
    accumulatedParticlesToAdd += ((weather == WEATHER_TYPE::SNOWING) ? 2.0 : 4.0) * gameTimeModVal;
  }

  auto numberToAdd = static_cast<unsigned>(accumulatedParticlesToAdd);
  accumulatedParticlesToAdd -= numberToAdd;

  /* Temporary stuff - just adds a few particles! */
  for (auto i = 0; i < numberToAdd; i++)
  {
    pos.x = playerPos.p.x;
    pos.z = playerPos.p.z;
    pos.x += world_coord(rand() % visibleTiles.x - visibleTiles.x / 2);
    pos.z += world_coord(rand() % visibleTiles.x - visibleTiles.y / 2);
    pos.y = 1000;

    /* If we've got one on the grid */
    if (pos.x <= 0 || pos.z <= 0 ||
        pos.x >= (SDWORD) world_coord(mapWidth - 1) ||
        pos.z >= (SDWORD) world_coord(mapHeight - 1)) {
      continue;
    }

    /* On grid, so which particle shall we add? */
    switch (weather) {
      case WEATHER_TYPE::SNOWING:
        atmosAddParticle(pos, PARTICLE_TYPE::SNOW);
      case WEATHER_TYPE::RAINING:
        atmosAddParticle(pos, PARTICLE_TYPE::RAIN);
        break;
      case WEATHER_TYPE::NONE:
        break;
    }
  }
}

void atmosDrawParticles(const glm::mat4& viewMatrix)
{
	UDWORD i;

	if (weather == WEATHER_TYPE::NONE)
	{
		return;
	}

	/* Traverse the list */
	for (i = 0; i < MAX_ATMOS_PARTICLES; i++)
	{
		/* Don't bother unless it's active */
		if (asAtmosParts[i].status == PARTICLE_STATUS::ACTIVE)
		{
			/* Is it visible on the screen? */
			if (clipXYZ(static_cast<int>(asAtmosParts[i].position.x), static_cast<int>(asAtmosParts[i].position.z),
			            static_cast<int>(asAtmosParts[i].position.y), viewMatrix)) {
				renderParticle(&asAtmosParts[i], viewMatrix);
			}
		}
	}
}

void renderParticle(Particle const* psPart, glm::mat4 const& viewMatrix)
{
	static glm::vec3 dv;

	/* Transform it */
	dv.x = psPart->position.x - playerPos.p.x;
	dv.y = psPart->position.y;
	dv.z = -(psPart->position.z - playerPos.p.z);
	/* Make it face camera */
	/* Scale it... */
	auto const modelMatrix = glm::translate(dv) *
		glm::rotate(UNDEG(-playerPos.r.y), glm::vec3(0.f, 1.f, 0.f)) *
		glm::rotate(UNDEG(-playerPos.r.x), glm::vec3(0.f, 1.f, 0.f)) *
		glm::scale(glm::vec3(psPart->size / 100.f));

	pie_Draw3DShape(psPart->imd.get(), 0, 0, WZCOL_WHITE, 0, 0, viewMatrix * modelMatrix);
}

void atmosSetWeatherType(WEATHER_TYPE type)
{
	if (type != weather) {
		weather = type;
		atmosInitSystem();
	}
	if (type == WEATHER_TYPE::NONE && !asAtmosParts.empty()) {
    asAtmosParts.clear();
	}
}

WEATHER_TYPE atmosGetWeatherType()
{
	return weather;
}
