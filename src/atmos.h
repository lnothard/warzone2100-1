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
 * @file atmos.h
 */

#ifndef __INCLUDED_SRC_ATMOS_H__
#define __INCLUDED_SRC_ATMOS_H__

enum class WEATHER_TYPE
{
    RAINING,
    SNOWING,
    NONE
};

enum class PARTICLE_TYPE
{
    RAIN,
    SNOW
};

enum class PARTICLE_STATUS
{
    INACTIVE,
    ACTIVE
};

struct Particle
{
	PARTICLE_STATUS status;
	PARTICLE_TYPE type;
	unsigned size;
	Vector3f position;
	Vector3f velocity;
	std::unique_ptr<iIMDShape> imd;
};


void atmosInitSystem();
void atmosUpdateSystem();
void renderParticle(Particle* psPart, const glm::mat4& viewMatrix);
void atmosDrawParticles(const glm::mat4& viewMatrix);
void atmosSetWeatherType(WEATHER_TYPE type);
WEATHER_TYPE atmosGetWeatherType();

#endif // __INCLUDED_SRC_ATMOS_H__
