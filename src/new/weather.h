//
// Created by Luna Nothard on 27/12/2021.
//

#ifndef WARZONE2100_WEATHER_H
#define WARZONE2100_WEATHER_H

#include <cstdint>
#include <memory>

#include "lib/framework/vector.h"
#include "lib/ivis_opengl/ivisdef.h"

#include "map.h"

static constexpr auto MAX_PARTICLES = MAP_MAXWIDTH * MAP_MAXHEIGHT;

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
    using enum PARTICLE_STATUS;
    PARTICLE_STATUS status = INACTIVE;
    PARTICLE_TYPE type;
    unsigned size = 0;
    Vector3i position {0, 0, 0};
    Vector3f velocity {0, 0, 0};
    std::unique_ptr<iIMDShape> imd_shape;
};

extern std::vector<Particle> particles;

/**
 * Makes a particle wrap around - if it goes off the grid, then it returns
 * on the other side - provided it's still on world... which it should be
 */
void wrap_particle(Particle& particle);

#endif //WARZONE2100_WEATHER_H
