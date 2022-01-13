//
// Created by luna on 10/12/2021.
//

#include "map.h"
#include "structure.h"

/// Reset
void aux_clear(int x, int y, int state)
{
  for (int i = 0; i < MAX_PLAYERS; ++i)
  {
    aux_map[i][x + y * map_width] &= ~state;
  }
}

void aux_set_all(int x, int y, int state)
{
  for (int i = 0; i < MAX_PLAYERS; ++i)
  {
    aux_map[i][x + y * map_width] |= state;
  }
}

void aux_set_enemy(int x, int y, unsigned player, int state)
{
  for (int i = 0; i < MAX_PLAYERS; ++i)
  {
    if (!(alliances[player] & (1 << i)))
      aux_map[i][x + y * map_width] |= state;
  }
}

void aux_set_allied(int x, int y, unsigned player, int state)
{
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    if (alliances[player] & (1 << i))
    {
      aux_map[i][x + y * map_width] |= state;
    }
  }
}

uint8_t get_terrain_type(const Tile& tile)
{
  return terrain_types[tile.texture & TILE_NUM_MASK];
}

bool tile_is_occupied(const Tile& tile)
{
  return tile.occupying_object != nullptr;
}

bool tile_is_occupied_by_structure(const Tile& tile)
{
  return tile_is_occupied(tile) && dynamic_cast<Structure*>(tile.occupying_object);
}

bool tile_is_occupied_by_feature(const Tile& tile)
{
  return tile_is_occupied(tile) && dynamic_cast<Feature*>(tile.occupying_object);
}

bool tile_visible_to_player(const Tile& tile, unsigned player)
{
  return tile.explored_bits & (1 << player);
}

bool tile_visible_to_selected_player(const Tile& tile)
{
  if (god_mode) {
    return true;
  }
  return tile_visible_to_player(tile, selectedPlayer);
}

Vector2i world_coord(const Vector2i& map_coord)
{
  return {world_coord(map_coord.x), world_coord(map_coord.y)};
}

Vector2i map_coord(const Vector2i& world_coord)
{
  return {map_coord(world_coord.x), map_coord(world_coord.y)};
}

static int calculate_map_height(const Vector2i& v)
{
  return calculate_map_height(v.x, v.y);
}

int map_tile_height(int x, int y)
{
  if (x >= map_width || y >= map_height || x < 0 || y < 0)
  {
    return 0;
  }
  return map_tiles[x + (y * map_width)].height;
}

void set_tile_height(int x, int y, int height)
{
  assert(x < map_width && x >=0);
  assert(y < map_height && y >= 0);

  map_tiles[x + (y * map_width)].height = height;
  mark_tile_dirty();
}

Tile* get_map_tile(int x, int y)
{
  x = MAX(x, 0);
  y = MAX(y, 0);
  x = MIN(x, map_width - 1);
  y = MIN(y, map_height - 1);

  return &map_tiles[x + (y * map_width)];
}

Tile* get_map_tile(const Vector2i& position)
{
  return get_map_tile(position.x, position.y);
}

Feature* get_feature_from_tile(int x, int y)
{
  auto* tile_object = get_map_tile(x, y)->occupying_object;
  return dynamic_cast<Feature*>(tile_object);
}

bool is_coord_on_map(int x, int y)
{
  return (x >= 0) && (x < map_width << TILE_SHIFT) &&
         (y >= 0) && (y < map_height << TILE_SHIFT);
}

bool is_coord_on_map(const Vector2i& position)
{
  return is_coord_on_map(position.x, position.y);
}

bool tile_on_map(int x, int y)
{
  return x >= 0 && x < map_width && y >= 0 && y < map_height;
}

bool tile_on_map(const Vector2i& position)
{
  return tile_on_map(position.x, position.y);
}

void clip_coords(Vector2i& pos)
{
  pos.x = MAX(1, pos.x);
  pos.y = MAX(1, pos.y);
  pos.x = MIN(world_coord(map_width) - 1, pos.x);
  pos.y = MIN(world_coord(map_height) - 1, pos.y);
}

uint8_t aux_tile(int x, int y, unsigned player)
{
  return aux_map[player][x + y * map_width];
}

uint8_t block_tile(int x, int y, int slot)
{
  return block_map[slot][x + y * map_width];
}

/**
 * Intersect a tile with a line and report the points of intersection
 * line is gives as point plus 2d directional vector
 * returned are two coordinates at the edge
 * true if the intersection also crosses the tile split line
 * (which has to be taken into account)
 **/
bool map_Intersect(int* Cx, int* Cy, int* Vx, int* Vy, int* Sx, int* Sy)
{
	int x, y, ox, oy, Dx, Dy, tileX, tileY;
	int ily, iry, itx, ibx;

	// dereference pointers
	x = *Cx;
	y = *Cy;
	Dx = *Vx;
	Dy = *Vy;

	/* Turn into tile coordinates */
	tileX = map_coord(x);
	tileY = map_coord(y);

	/* Inter tile comp */
	ox = map_round(x);
	oy = map_round(y);

	/* allow backwards tracing */
	if (ox == 0 && Dx < 0)
	{
		tileX--;
		ox = TILE_UNITS;
	}
	if (oy == 0 && Dy < 0)
	{
		tileY--;
		oy = TILE_UNITS;
	}

	*Cx = -4 * TILE_UNITS; // to trigger assertion
	*Cy = -4 * TILE_UNITS;
	*Vx = -4 * TILE_UNITS;
	*Vy = -4 * TILE_UNITS;

	// calculate intersection point on the left and right (if any)
	ily = y - 4 * TILE_UNITS; // make sure initial value is way outside of tile
	iry = y - 4 * TILE_UNITS;
	if (Dx != 0)
	{
		ily = y - ox * Dy / Dx;
		iry = y + (TILE_UNITS - ox) * Dy / Dx;
	}
	// calculate intersection point on top and bottom (if any)
	itx = x - 4 * TILE_UNITS; // make sure initial value is way outside of tile
	ibx = x - 4 * TILE_UNITS;
	if (Dy != 0)
	{
		itx = x - oy * Dx / Dy;
		ibx = x + (TILE_UNITS - oy) * Dx / Dy;
	}

	// line comes from the left?
	if (Dx >= 0)
	{
		if (map_coord(ily) == tileY || map_coord(ily - 1) == tileY)
		{
			*Cx = world_coord(tileX);
			*Cy = ily;
		}
		if (map_coord(iry) == tileY || map_coord(iry - 1) == tileY)
		{
			*Vx = world_coord(tileX + 1);
			*Vy = iry;
		}
	}
	else
	{
		if (map_coord(ily) == tileY || map_coord(ily - 1) == tileY)
		{
			*Vx = world_coord(tileX);
			*Vy = ily;
		}
		if (map_coord(iry) == tileY || map_coord(iry - 1) == tileY)
		{
			*Cx = world_coord(tileX + 1);
			*Cy = iry;
		}
	}
	// line comes from the top?
	if (Dy >= 0)
	{
		if (map_coord(itx) == tileX || map_coord(itx - 1) == tileX)
		{
			*Cx = itx;
			*Cy = world_coord(tileY);
		}
		if (map_coord(ibx) == tileX || map_coord(ibx - 1) == tileX)
		{
			*Vx = ibx;
			*Vy = world_coord(tileY + 1);
		}
	}
	else
	{
		if (map_coord(itx) == tileX || map_coord(itx - 1) == tileX)
		{
			*Vx = itx;
			*Vy = world_coord(tileY);
		}
		if (map_coord(ibx) == tileX || map_coord(ibx - 1) == tileX)
		{
			*Cx = ibx;
			*Cy = world_coord(tileY + 1);
		}
	}
	// assertions, no intersections outside of tile
	ASSERT(*Cx >= world_coord(tileX) && *Cx <= world_coord(tileX + 1),
	       "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
	ASSERT(*Cy >= world_coord(tileY) && *Cy <= world_coord(tileY + 1),
	       "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
	ASSERT(*Vx >= world_coord(tileX) && *Vx <= world_coord(tileX + 1),
	       "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
	ASSERT(*Vy >= world_coord(tileY) && *Vy <= world_coord(tileY + 1),
	       "map_Intersect(): tile Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);
	ASSERT(tileX >= 0 && tileY >= 0 && tileX < map_width && tileY < map_height,
	       "map_Intersect(): map Bounds %i %i, %i %i -> %i,%i,%i,%i", x, y, Dx, Dy, *Cx, *Cy, *Vx, *Vy);

	//calculate midway line intersection points
	if (((map_coord(itx) == tileX) == (map_coord(ily) == tileY)) && ((map_coord(ibx) == tileX) == (map_coord(iry) ==
		tileY)))
	{
		// line crosses diagonal only
		if (Dx - Dy == 0)
		{
			return false;
		}
		*Sx = world_coord(tileX) + (Dx * oy - Dy * ox) / (Dx - Dy);
		*Sy = world_coord(tileY) + (Dx * oy - Dy * ox) / (Dx - Dy);
		if (map_coord(*Sx) != tileX || map_coord(*Sy) != tileY)
		{
			return false;
		}
		return true;
	}
	else if (((map_coord(ibx) == tileX) == (map_coord(ily) == tileY)) && ((map_coord(itx) == tileX) == (map_coord(iry)
		== tileY)))
	{
		//line crosses anti-diagonal only
		if (Dx + Dy == 0)
		{
			return false;
		}
		*Sx = world_coord(tileX) + (Dx * (TILE_UNITS - oy) + Dy * ox) / (Dx + Dy);
		*Sy = world_coord(tileY) + (Dy * (TILE_UNITS - ox) + Dx * oy) / (Dx + Dy);
		if (map_coord(*Sx) != tileX || map_coord(*Sy) != tileY)
		{
			return false;
		}
		return true;
	}
	else
	{
		//line crosses both tile diagonals
		//TODO: trunk divides tiles into 4 parts instead of 2 in 2.3.
		//We would need to check and return both intersections here now,
		//but that would require an additional return parameter!
		//Instead we check only one of them and know it might be wrong!
		if (Dx + Dy != 0)
		{
			// check anti-diagonal
			*Sx = world_coord(tileX) + (Dx * (TILE_UNITS - oy) + Dy * ox) / (Dx + Dy);
			*Sy = world_coord(tileY) + (Dy * (TILE_UNITS - ox) + Dx * oy) / (Dx + Dy);
			if (map_coord(*Sx) == tileX && map_coord(*Sy) == tileY)
			{
				return true;
			}
		}
		if (Dx - Dy != 0)
		{
			// check diagonal
			*Sx = world_coord(tileX) + (Dx * oy - Dy * ox) / (Dx - Dy);
			*Sy = world_coord(tileY) + (Dx * oy - Dy * ox) / (Dx - Dy);
			if (map_coord(*Sx) == tileX && map_coord(*Sy) == tileY)
			{
				return true;
			}
		}
	}

	return false;
}
