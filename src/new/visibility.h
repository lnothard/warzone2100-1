//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_VISIBILITY_H
#define WARZONE2100_VISIBILITY_H

#include "basedef.h"
#include "map.h"

static constexpr auto VISIBILITY_INCREASE_RATE = 255 * 2;
static constexpr auto VISIBILITY_DECREASE_RATE = 50;
static constexpr auto MIN_VISIBILITY_HEIGHT = 80;
static constexpr auto BASE_DIVISOR = 8;
static constexpr auto MIN_ILLUMINATION = 45.0f;
static constexpr auto FADE_IN_TIME = GAME_TICKS_PER_SEC / 10;

/// Whether unexplored tiles should be shown as just darker fog. Left here as a future option
/// for scripts, since campaign may still want total darkness on unexplored tiles.
static bool active_reveal = true;

enum class SENSOR_CLASS
{
    VISION,
    RADAR
};

struct Spotter
{
    SENSOR_CLASS sensor_type;
    Position position;
    unsigned player;
    int sensor_radius;
    /// when to self-destruct; zero if never
    std::size_t expiration_time;
};
extern std::vector<Spotter> invisible_viewers;

inline bool get_reveal_status()
{
  return active_reveal;
}

inline bool set_reveal_status(bool value)
{
  active_reveal = value;
}

bool objects_in_vis_range(const SimpleObject& first,
                          const SimpleObject& second,
                          int range);

void update_tile_visibility();

void update_tile_sensors(Tile& tile);

unsigned get_object_light_level(const SimpleObject& object,
                                unsigned original_level);

#endif // WARZONE2100_VISIBILITY_H
