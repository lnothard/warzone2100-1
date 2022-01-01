//
// Created by Luna Nothard on 27/12/2021.
//

#include "display3d.h"
#include "weather.h"

void wrap_particle(Particle& particle)
{
  // Gone off left side
  if (particle.position.x < player_pos.position.x -
      world_coord(visible_tiles.x) / 2) {
    particle.position.x += world_coord(visible_tiles.x);
  }
  // Gone off right side
  else if (particle.position.x > (player_pos.position.x +
           world_coord(visible_tiles.x) / 2)) {
    particle.position.x -= world_coord(visible_tiles.x);
  }

  // Gone off top
  if (particle.position.z < player_pos.position.z -
      world_coord(visible_tiles.y) / 2) {
    particle.position.z += world_coord(visible_tiles.y);
  }
  // Gone off bottom
  else if (particle.position.z > (player_pos.position.z +
           world_coord(visible_tiles.y) / 2))
  {
    particle.position.z -= world_coord(visible_tiles.y);
  }
}