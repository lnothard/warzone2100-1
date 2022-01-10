//
// Created by Luna Nothard on 31/12/2021.
//

#include "visibility.h"

bool objects_in_vis_range(const SimpleObject& first, const SimpleObject& second, int range)
{
  const auto x_diff = first.getPosition().x - second.getPosition().x;
  const auto y_diff = first.getPosition().y - second.getPosition().y;

  return abs(x_diff) <= range && x_diff * x_diff + y_diff * y_diff <= range;
}

void preprocess_visibility()
{
  for (int i = 0; i < map_width; ++i)
  {
    for (int j = 0; j < map_height; ++j)
    {
      auto tile = get_map_tile(i, j);
      tile->visibility_level = active_reveal ?
              MIN(MIN_ILLUMINATION, tile->illumination_level / 4.0f) : 0;

      if (tile_visible_to_selected_player(*tile)) {
        tile->visibility_level = tile->illumination_level;
      }
    }
  }
}

void update_tile_visibility()
{
  const auto area = map_height * map_width;
  const auto player_mask = 1 << selectedPlayer;
  auto increment = graphicsTimeAdjustedIncrement(FADE_IN_TIME);

  std::for_each(map_tiles.begin(), map_tiles.end(), [&](auto& tile)
  {
    auto max_level = tile.illuminationLevel;
    // if seen
    if (tile.visibility_level > MIN_ILLUMINATION ||
        tile.explored_bits & player_mask) {
      // If we are not omniscient, and we are not seeing the tile,
      // and none of our allies see the tile...
      if (!god_mode && !alliance_bits[selectedPlayer] &
                               (satellite_uplink_bits | tile.sensor_bits)) {
        max_level /= 2;
      }
      if (tile.visibility_level > max_level) {
        tile.visibility_level = MAX(tile.visibility_level - increment, max_level);
      } else if (tile.visibility_level < max_level) {
        tile.visibility_level = MIN(tile.visibility_level + increment, max_level);
      }
    }
  });
}

void update_tile_sensors(Tile& tile)
{
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    /// The definition of whether a player can see something on a given tile or not
    if (tile.watchers[i] > 0 || (tile.watching_sensors[i] > 0 &&
        !(tile.jammer_bits & ~alliance_bits[i]))) {
      tile.sensor_bits |= (1 << i); // mark it as being seen
    } else {
      tile.sensor_bits &= ~(1 << i); // mark as hidden
    }
  }
}

unsigned get_object_light_level(const SimpleObject& object, unsigned original_level)
{
  const auto divisor = object.visibleToSelectedPlayer() / 255.f;
  const auto lowest_level = original_level / BASE_DIVISOR;

  auto new_level = static_cast<unsigned>(divisor * original_level);
  if (new_level < lowest_level) {
    new_level = lowest_level;
  }
  return new_level;
}
