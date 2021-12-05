//
// Created by luna on 05/12/2021.
//

#include "featuredef.h"

/* Remove a Feature and free it's memory */
bool Feature::destroyFeature(unsigned impactTime)
{
  UDWORD			widthScatter, breadthScatter, heightScatter, i;
  EFFECT_TYPE		explosionSize;
  Vector3i pos;

  ASSERT_OR_RETURN(false, psDel != nullptr, "Invalid feature pointer");
  ASSERT(gameTime - deltaGameTime < impactTime, "Expected %u < %u, gameTime = %u, bad impactTime", gameTime - deltaGameTime, impactTime, gameTime);

  /* Only add if visible and damageable*/
  if (visibleForLocalDisplay() && psStats->damageable)
  {
    /* Set off a destruction effect */
    /* First Explosions */
    widthScatter = TILE_UNITS / 2;
    breadthScatter = TILE_UNITS / 2;
    heightScatter = TILE_UNITS / 4;
    //set which explosion to use based on size of feature
    if (psStats->baseWidth < 2 && psStats->baseBreadth < 2)
    {
      explosionSize = EXPLOSION_TYPE_SMALL;
    }
    else if (psStats->baseWidth < 3 && psStats->baseBreadth < 3)
    {
      explosionSize = EXPLOSION_TYPE_MEDIUM;
    }
    else
    {
      explosionSize = EXPLOSION_TYPE_LARGE;
    }
    for (i = 0; i < 4; i++)
    {
      pos.x = position.x + widthScatter - rand() % (2 * widthScatter);
      pos.z = position.y + breadthScatter - rand() % (2 * breadthScatter);
      pos.y = position.z + 32 + rand() % heightScatter;
      addEffect(&pos, EFFECT_EXPLOSION, explosionSize, false, nullptr, 0, impactTime);
    }

    if (psStats->subType == FEATURE_TYPE::SKYSCRAPER)
    {
      pos.x = position.x;
      pos.z = position.y;
      pos.y = position.z;
      addEffect(&pos, EFFECT_DESTRUCTION, DESTRUCTION_TYPE_SKYSCRAPER, true, displayData.imd, 0, impactTime);
      initPerimeterSmoke(displayData.imd, pos);

      shakeStart(250); // small shake
    }

    /* Then a sequence of effects */
    pos.x = position.x;
    pos.z = position.y;
    pos.y = map_Height(pos.x, pos.z);
    addEffect(&pos, EFFECT_DESTRUCTION, DESTRUCTION_TYPE_FEATURE, false, nullptr, 0, impactTime);

    //play sound
    // ffs gj
    if (psStats->subType == FEATURE_TYPE::SKYSCRAPER)
    {
      audio_PlayStaticTrack(position.x, position.y, ID_SOUND_BUILDING_FALL);
    }
    else
    {
      audio_PlayStaticTrack(position.x, position.y, ID_SOUND_EXPLOSION);
    }
  }

  if (psStats->subType == FEATURE_TYPE::SKYSCRAPER)
  {
    // ----- Flip all the tiles under the skyscraper to a rubble tile
    // smoke effect should disguise this happening
    StructureBounds b = getStructureBounds(psDel);
    for (int breadth = 0; breadth < b.size.y; ++breadth)
    {
      for (int width = 0; width < b.size.x; ++width)
      {
        MAPTILE *psTile = mapTile(b.map.x + width, b.map.y + breadth);
        // stops water texture changing for underwater features
        if (terrainType(psTile) != TER_WATER)
        {
          if (terrainType(psTile) != TER_CLIFFFACE)
          {
            /* Clear feature bits */
            psTile->texture = TileNumber_texture(psTile->texture) | RUBBLE_TILE;
            auxClearBlocking(b.map.x + width, b.map.y + breadth, AUXBITS_ALL);
          }
          else
          {
            /* This remains a blocking tile */
            psTile->psObject = nullptr;
            auxClearBlocking(b.map.x + width, b.map.y + breadth, AIR_BLOCKED);  // Shouldn't remain blocking for air units, however.
            psTile->texture = TileNumber_texture(psTile->texture) | BLOCKING_RUBBLE_TILE;
          }
        }
      }
    }
  }

  removeFeature(psDel);
  deathTime = impactTime;
  return true;
}