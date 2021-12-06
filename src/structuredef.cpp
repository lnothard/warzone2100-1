//
// Created by luna on 02/12/2021.
//

#include "structuredef.h"
#include "projectile.h"
#include "stats.h"

StructureStats::StructureStats() : pBaseIMD(nullptr), pECM(nullptr), pSensor(nullptr)
{
  memset(curCount, 0, sizeof(curCount));
  memset(upgrade, 0, sizeof(upgrade));
}

Vector2i StructureStats::size(uint16_t direction) const
{
  Vector2i size(baseWidth, baseBreadth);
  if ((snapDirection(direction) & 0x4000) != 0) // if building is rotated left or right by 90°, swap width and height
  {
    std::swap(size.x, size.y);
  }
  return size;
}


Vector2i Structure::size() const
{
  return stats->size(m_rotation.direction);
}

// see if a structure has the range to fire on a target
bool Structure::aiUnitHasRange(const GameObject& targetObj, int weapon_slot)
{
  if (numWeapons == 0 || m_weaponList[0].nStat == 0)
  {
    // Can't attack without a weapon
    return false;
  }

  WEAPON_STATS *psWStats = m_weaponList[weapon_slot].nStat + asWeaponStats;

  int longRange = proj_GetLongRange(psWStats, owningPlayer);
  return objPosDiffSq(targetObj) < longRange * longRange && lineOfFire(this, targetObj, weapon_slot, true);
}

void Structure::addConstructorEffect()
{
  if ((ONEINTEN) && (visibleForLocalDisplay()))
  {
    /* This needs fixing - it's an arse effect! */
    const Vector2i size = size() * TILE_UNITS / 4;
    Vector3i temp;
    temp.x = position.x + ((rand() % (2 * size.x)) - size.x);
    temp.y = map_TileHeight(map_coord(position.x), map_coord(position.y)) + (displayData.imd->max.y / 6);
    temp.z = position.y + ((rand() % (2 * size.y)) - size.y);
    if (rand() % 2)
    {
      droidAddWeldSound(temp);
    }
  }
}

void Structure::alignStructure()
{
  /* DEFENSIVE structures are pulled to the terrain */
  if (!isPulledToTerrain(psBuilding))
  {
    int mapH = foundationHeight(psBuilding);

    buildFlatten(psBuilding, mapH);
    position.z = mapH;
    foundationDepth = position.z;

    // Align surrounding structures.
    StructureBounds b = getStructureBounds(psBuilding);
    syncDebug("Flattened (%d+%d, %d+%d) to %d for %d(p%d)", b.map.x, b.size.x, b.map.y, b.size.y, mapH, id, owningPlayer);
    for (int breadth = -1; breadth <= b.size.y; ++breadth)
    {
      for (int width = -1; width <= b.size.x; ++width)
      {
        Structure *neighbourStructure = castStructure(mapTile(b.map.x + width, b.map.y + breadth)->psObject);
        if (neighbourStructure != nullptr && isPulledToTerrain(neighbourStructure))
        {
          alignStructure(neighbourStructure);  // Recursive call, but will go to the else case, so will not re-recurse.
        }
      }
    }
  }
  else
  {
    // Sample points around the structure to find a good depth for the foundation
    iIMDShape *s = displayData.imd;

    position.z = TILE_MIN_HEIGHT;
    foundationDepth = TILE_MAX_HEIGHT;

    Vector2i dir = iSinCosR(rotation.direction, 1);
    // Rotate s->max.{x, z} and s->min.{x, z} by angle rot.direction.
    Vector2i p1{s->max.x * dir.y - s->max.z * dir.x, s->max.x * dir.x + s->max.z * dir.y};
    Vector2i p2{s->min.x * dir.y - s->min.z * dir.x, s->min.x * dir.x + s->min.z * dir.y};

    int h1 = map_Height(position.x + p1.x,
                        position.y + p2.y);
    int h2 = map_Height(position.x + p1.x,
                        position.y + p1.y);
    int h3 = map_Height(position.x + p2.x,
                        position.y + p1.y);
    int h4 = map_Height(position.x + p2.x,
                        position.y + p2.y);
    int minH = std::min({h1, h2, h3, h4});
    int maxH = std::max({h1, h2, h3, h4});
    position.z =
        std::max(position.z, maxH);
    foundationDepth = std::min<float>(foundationDepth, minH);
    syncDebug("minH=%d,maxH=%d,pointHeight=%d", minH, maxH, position
                                                                .z);  // s->max is based on floats! If this causes desynchs, need to fix!
  }
}

// Power returned on demolish, which is half the power taken to build the structure and any modules
int Structure::structureTotalReturn() const
{
  int power = stats->powerToBuild;

  const StructureStats * const moduleStats = getModuleStat(psStruct);

  if (nullptr != moduleStats)
  {
    power += capacity * moduleStats->powerToBuild;
  }

  return power / 2;
}

void Factory::refundFactoryBuildPower()
{
  if (psSubject)
  {
    if (buildPointsRemaining < (int)calcTemplateBuild(psSubject))
    {
      // We started building, so give the power back that was used.
      addPower(owningPlayer, calcTemplatePower(psSubject));
    }

  }
}

/* Set the type of droid for a factory to build */
bool Factory::structSetManufacture(DroidStats *psTempl, QUEUE_MODE mode)
{
  CHECK_STRUCTURE(psStruct);

  /* psTempl might be NULL if the build is being cancelled in the middle */
  ASSERT_OR_RETURN(false, !psTempl
                              || (validTemplateForFactory(psTempl, psStruct, true) && researchedTemplate(psTempl, owningPlayer, true, true))
                              || owningPlayer == scavengerPlayer() || !bMultiPlayer,
                   "Wrong template for player %d factory, getType %d.", owningPlayer, stats->type);

  if (mode == ModeQueue)
  {
    sendStructureInfo(psStruct, STRUCTUREINFO_MANUFACTURE, psTempl);
    setStatusPendingStart(*psFact, psTempl);
    return true;  // Wait for our message before doing anything.
  }

  //assign it to the Factory
  refundFactoryBuildPower();
  psSubject = psTempl;

  //set up the start time and build time
  if (psTempl != nullptr)
  {
    //only use this for non selectedPlayer
    if (owningPlayer != selectedPlayer)
    {
      //set quantity to produce
      productionLoops = 1;
    }

    timeStarted = ACTION_START_TIME;//gameTime;
    timeStartHold = 0;

    buildPointsRemaining = calcTemplateBuild(psTempl);
    //check for zero build time - usually caused by 'silly' data! If so, set to 1 build point - ie very fast!
    buildPointsRemaining = std::max(buildPointsRemaining, 1);
  }
  return true;
}

bool Factory::isCommanderGroupFull() const
{
  if (bMultiPlayer)
  {
    // TODO: Synchronise .psCommander. Have to return false here, to avoid desynch.
    return false;
  }

  unsigned int DroidsInGroup;

  // If we don't have a commander return false (group not full)
  if (psCommander == nullptr)
  {
    return false;
  }

  // allow any number of IDF droids
  if (templateIsIDF(psSubject) || asPropulsionStats[psSubject->asParts[COMP_PROPULSION]].propulsionType == PROPULSION_TYPE_LIFT)
  {
    return false;
  }

  // Get the number of droids in the commanders group
  DroidsInGroup = psCommander->psGroup ? psCommander->psGroup->getNumMembers() : 0;

  // if the number in group is less than the maximum allowed then return false (group not full)
  if (DroidsInGroup < cmdDroidMaxGroup(psCommander))
  {
    return false;
  }

  // the number in group has reached the maximum
  return true;
}


/// Add buildPoints to the structures currentBuildPts, due to construction work by the droid
/// Also can deconstruct (demolish) a building if passed negative buildpoints
void Structure::structureBuild(Droid *psDroid, int buildPoints, int buildRate)
{
  bool checkResearchButton = status == STRUCT_STATES::BUILT;  // We probably just started demolishing, if this is true.
  int prevResearchState = 0;
  if (checkResearchButton)
  {
    prevResearchState = intGetResearchState();
  }

  if (psDroid && !aiCheckAlliances(owningPlayer, psDroid->owningPlayer))
  {
    // Enemy structure
    return;
  }
  else if (stats->type != REF_FACTORY_MODULE)
  {
    for (unsigned player = 0; player < MAX_PLAYERS; player++)
    {
      for (Droid *psCurr = allDroidLists[player]; psCurr != nullptr; psCurr = psCurr->psNext)
      {
        // An enemy droid is blocking it
        if ((Structure *) orderStateObj(psCurr, DORDER_BUILD) == psStruct
            && !aiCheckAlliances(owningPlayer, psCurr->owningPlayer))
        {
          return;
        }
      }
    }
  }
  buildRate += buildRate;  // buildRate = buildPoints/GAME_UPDATES_PER_SEC, but might be rounded up or down each tick, so can't use buildPoints to get a stable number.
  if (currentBuildPts <= 0 && buildPoints > 0)
  {
    // Just starting to build structure, need power for it.
    bool haveEnoughPower = requestPowerFor(psStruct, structPowerToBuildOrAddNextModule(psStruct));
    if (!haveEnoughPower)
    {
      buildPoints = 0;  // No power to build.
    }
  }

  int newBuildPoints = currentBuildPts + buildPoints;
  ASSERT(newBuildPoints <= 1 + 3 * (int)structureBuildPointsToCompletion(*psStruct), "unsigned int underflow?");
  CLIP(newBuildPoints, 0, structureBuildPointsToCompletion(*psStruct));

  if (currentBuildPts > 0 && newBuildPoints <= 0)
  {
    // Demolished structure, return some power.
    addPower(owningPlayer, structureTotalReturn(psStruct));
  }

  ASSERT(newBuildPoints <= 1 + 3 * (int)structureBuildPointsToCompletion(*psStruct), "unsigned int underflow?");
  CLIP(newBuildPoints, 0, structureBuildPointsToCompletion(*psStruct));

  int deltaBody = quantiseFraction(9 * structureBody(psStruct), 10 * structureBuildPointsToCompletion(*psStruct), newBuildPoints, currentBuildPts);
  currentBuildPts = newBuildPoints;
  hitPoints = std::max<int>(hitPoints + deltaBody, 1);

  //check if structure is built
  if (buildPoints > 0 && currentBuildPts >= structureBuildPointsToCompletion(*psStruct))
  {
    buildingComplete(psStruct);

    //only play the sound if selected player
    if (psDroid &&
        owningPlayer == selectedPlayer
        && (psDroid->order.type != DORDER_LINEBUILD
            || map_coord(psDroid->order.pos) == map_coord(psDroid->order.pos2)))
    {
      audio_QueueTrackPos(
          ID_SOUND_STRUCTURE_COMPLETED, getPosition.x,
          getPosition.y, getPosition.z);
      intRefreshScreen();		// update any open interface bars.
    }

    /* must reset here before the callback, droid must have DACTION_NONE
         in order to be able to start a new built task, doubled in actionUpdateDroid() */
    if (psDroid)
    {
      Droid *psIter;

      // Clear all orders for helping hands. Needed for AI script which runs next frame.
      for (psIter = allDroidLists[psDroid->owningPlayer]; psIter; psIter = psIter->psNext)
      {
        if ((psIter->order.type == DORDER_BUILD || psIter->order.type == DORDER_HELPBUILD || psIter->order.type == DORDER_LINEBUILD)
            && psIter->order.psObj == psStruct
            && (psIter->order.type != DORDER_LINEBUILD || map_coord(psIter->order.pos) == map_coord(psIter->order.pos2)))
        {
          objTrace(psIter->id, "Construction order %s complete (%d, %d -> %d, %d)", getDroidOrderName(psDroid->order.type),
                   psIter->order.pos2.x, psIter->order.pos.y, psIter->order.pos2.x, psIter->order.pos2.y);
          psIter->action = DACTION_NONE;
          psIter->order = DroidOrder(DORDER_NONE);
          setDroidActionTarget(psIter, nullptr, 0);
        }
      }

      audio_StopObjTrack(psDroid, ID_SOUND_CONSTRUCTION_LOOP);
    }
    triggerEventStructBuilt(psStruct, psDroid);
    checkPlayerBuiltHQ(psStruct);
  }
  else
  {
    STRUCT_STATES prevStatus = status;
    status = SS_BEING_BUILT;
    if (prevStatus == SS_BUILT)
    {
      // Starting to demolish.
      triggerEventStructDemolish(psStruct, psDroid);
      if (owningPlayer == selectedPlayer)
      {
        intRefreshScreen();
      }

      switch (stats->type)
      {
      case REF_POWER_GEN:
        releasePowerGen(psStruct);
        break;
      case REF_RESOURCE_EXTRACTOR:
        releaseResExtractor(psStruct);
        break;
      default:
        break;
      }
    }
  }
  if (buildPoints < 0 && currentBuildPts == 0)
  {
    triggerEvent(TRIGGER_OBJECT_RECYCLED, psStruct);
    removeStruct(psStruct, true);
  }

  if (checkResearchButton)
  {
    intNotifyResearchButton(prevResearchState);
  }
}

/* The main update routine for all Structures */
void Structure::structureUpdate(bool bMission) {
  UDWORD widthScatter, breadthScatter;
  UDWORD emissionInterval, iPointsToAdd, iPointsRequired;
  Vector3i dv;
  int i;

  syncDebugStructure(psBuilding, '<');

  if (flags.test(DIRTY) && !bMission) {
    visTilesUpdate(psBuilding);
    flags.set(DIRTY, false);
  }

  if (stats->type == REF_GATE) {
    if (state == SAS_OPEN &&
        lastStateTime + SAS_STAY_OPEN_TIME < gameTime) {
      bool found = false;

      static GridList gridList; // static to avoid allocations.
      gridList = gridStartIterate(position.x,
                                  position.y, TILE_UNITS);
      for (GridIterator gi = gridList.begin(); !found && gi != gridList.end();
           ++gi) {
        found = isDroid(*gi);
      }

      if (!found) // no droids on our tile, safe to close
      {
        state = SAS_CLOSING;
        auxStructureClosedGate(psBuilding);   // closed
        lastStateTime = gameTime; // reset timer
      }
    } else if (state == SAS_OPENING &&
               lastStateTime + SAS_OPEN_SPEED < gameTime) {
      state = SAS_OPEN;
      auxStructureOpenGate(psBuilding);     // opened
      lastStateTime = gameTime; // reset timer
    } else if (state == SAS_CLOSING &&
               lastStateTime + SAS_OPEN_SPEED < gameTime) {
      state = SAS_NORMAL;
      lastStateTime = gameTime; // reset timer
    }
  } else if (stats->type == REF_RESOURCE_EXTRACTOR) {
    if (!pFunctionality->resourceExtractor.psPowerGen &&
        animationEvent ==
            ANIM_EVENT_ACTIVE) // no power generator connected
    {
      timeAnimationStarted = 0; // so turn off animation, if any
      animationEvent = ANIM_EVENT_NONE;
    } else if (pFunctionality->resourceExtractor.psPowerGen &&
               animationEvent ==
                   ANIM_EVENT_NONE) // we have a power generator, but no animation
    {
      animationEvent = ANIM_EVENT_ACTIVE;

      iIMDShape *strFirstImd =
          displayData.imd->objanimpie[animationEvent];
      if (strFirstImd != nullptr && strFirstImd->next != nullptr) {
        iIMDShape *strImd = strFirstImd->next; // first imd isn't animated
        timeAnimationStarted =
            gameTime +
            (rand() % (strImd->objanimframes *
                       strImd->objanimtime)); // vary animation start time
      } else {
        ASSERT(strFirstImd != nullptr && strFirstImd->next != nullptr,
               "Unexpected objanimpie");
        timeAnimationStarted = gameTime; // so start animation
      }
    }

    if (owningPlayer == selectedPlayer) {
      if (psBuilding
              ->visibleForLocalDisplay() // check for display(audio)-only - does not impact simulation / game state
          && pFunctionality->resourceExtractor.psPowerGen &&
          animationEvent == ANIM_EVENT_ACTIVE) {
        audio_PlayObjStaticTrack(psBuilding, ID_SOUND_OIL_PUMP_2);
      } else {
        audio_StopObjTrack(psBuilding, ID_SOUND_OIL_PUMP_2);
      }
    }
  }

  // Remove invalid targets. This must be done each frame.
  for (i = 0; i < MAX_WEAPONS; i++) {
    if (psTarget[i] && psTarget[i]->deathTime) {
      syncDebugObject(psTarget[i], '-');
      setStructureTarget(psBuilding, nullptr, i, ORIGIN_UNKNOWN);
    }
  }

  // update the manufacture/research of the building once complete
  if (status == SS_BUILT) {
    aiUpdateStructure(psBuilding, bMission);
  }

  if (status != SS_BUILT) {
    if (selected) {
      selected = false;
    }
  }

  if (!bMission) {
    if (status == SS_BEING_BUILT && buildRate == 0 &&
        !structureHasModules(psBuilding)) {
      if (stats->powerToBuild == 0) {
        // Building is free, and not currently being built, so deconstruct slowly over 1 minute.
        currentBuildPts -= std::min<int>(
            currentBuildPts,
            gameTimeAdjustedAverage(
                structureBuildPointsToCompletion(*psBuilding), 60));
      }

      if (currentBuildPts == 0) {
        removeStruct(psBuilding,
                     true); // If giving up on building something, remove the structure (and remove it from the power queue).
      }
    }
    lastBuildRate = buildRate;
    buildRate =
        0; // Reset to 0, each truck building us will add to our buildRate.
  }

  /* Only add smoke if they're visible and they can 'burn' */
  if (!bMission && visibleForLocalDisplay() &&
      canSmoke(psBuilding)) {
    const int32_t damage = getStructureDamage(psBuilding);

    // Is there any damage?
    if (damage > 0.) {
      emissionInterval =
          static_cast<UDWORD>(CalcStructureSmokeInterval(damage / 65536.f));
      unsigned effectTime =
          std::max(gameTime - deltaGameTime + 1,
                   lastEmission + emissionInterval);
      if (gameTime >= effectTime) {
        const Vector2i size = size();
        widthScatter = size.x * TILE_UNITS / 2 / 3;
        breadthScatter = size.y * TILE_UNITS / 2 / 3;
        dv.x = position.x + widthScatter -
               rand() % (2 * widthScatter);
        dv.z = position.y + breadthScatter -
               rand() % (2 * breadthScatter);
        dv.y = position.z;
        dv.y += (displayData.imd->max.y * 3) / 4;
        addEffect(&dv, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH, false, nullptr,
                  0, effectTime);
        lastEmission = effectTime;
      }
    }
  }

  /* Update the fire damage data */
  if (periodicalDamageStart != 0 &&
      periodicalDamageStart !=
          gameTime - deltaGameTime) // -deltaGameTime, since projectiles are updated after structures.
  {
    // The periodicalDamageStart has been set, but is not from the previous tick, so we must be out of the fire.
    periodicalDamage = 0; // Reset burn damage done this tick.
    // Finished burning.
    periodicalDamageStart = 0;
  }

  // check the resistance level of the structure
  iPointsRequired =
      structureResistance(stats, owningPlayer);
  if (resistance < (SWORD)iPointsRequired) {
    // start the resistance increase
    if (lastResistance == ACTION_START_TIME) {
      lastResistance = gameTime;
    }
    // increase over time if low
    if ((gameTime - lastResistance) > RESISTANCE_INTERVAL) {
      resistance++;

      // in multiplayer, certain structures do not function whilst low resistance
      if (bMultiPlayer) {
        resetResistanceLag(psBuilding);
      }

      lastResistance = gameTime;
      // once the resistance is back up reset the last time increased
      if (resistance >= (SWORD)iPointsRequired) {
        lastResistance = ACTION_START_TIME;
      }
    }
  } else {
    // if selfrepair has been researched then check the health level of the
    // structure once resistance is fully up
    iPointsRequired = structureBody(psBuilding);
    if (selfRepairEnabled(owningPlayer) &&
        hitPoints < iPointsRequired &&
        status != SS_BEING_BUILT) {
      // start the self repair off
      if (lastResistance == ACTION_START_TIME) {
        lastResistance = gameTime;
      }

      /*since self repair, then add half repair points depending on the time delay for the stat*/
      iPointsToAdd =
          (repairPoints(asRepairStats +
                            aDefaultRepair[owningPlayer],
                        owningPlayer) /
           4) *
          ((gameTime - lastResistance) /
           (asRepairStats + aDefaultRepair[owningPlayer])->time);

      // add the blue flashing effect for multiPlayer
      if (bMultiPlayer && ONEINTEN && !bMission) {
        Vector3i position;
        Vector3f *point;
        SDWORD realY;
        UDWORD pointIndex;

        pointIndex = rand() % (displayData.imd->points.size() - 1);
        point = &(displayData.imd->points.at(pointIndex));
        position.x = static_cast<int>(getPosition.x + point->x);
        realY = static_cast<SDWORD>(structHeightScale(psBuilding) * point->y);
        position.y = getPosition.z + realY;
        position.z = static_cast<int>(getPosition.y - point->z);
        const auto psTile = mapTile(map_coord({position.x, position.y}));
        if (tileIsClearlyVisible(psTile)) {
          effectSetSize(30);
          addEffect(&position, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED, true,
                    getImdFromIndex(MI_PLASMA), 0,
                    gameTime - deltaGameTime + rand() % deltaGameTime);
        }
      }

      if (iPointsToAdd) {
        hitPoints = (UWORD)(hitPoints + iPointsToAdd);
        lastResistance = gameTime;
        if (hitPoints > iPointsRequired) {
          hitPoints = (UWORD)iPointsRequired;
          lastResistance = ACTION_START_TIME;
        }
      }
    }
  }

  syncDebugStructure(psBuilding, '>');

  CHECK_STRUCTURE(psBuilding);
}

int Structure::requestOpenGate()
{
  if (status != STRUCT_STATES::BUILT || stats->type != STRUCTURE_TYPE::GATE)
  {
    return 0;  // Can't open.
  }

  switch (state)
  {
  case STRUCT_ANIM_STATES::NORMAL:
    lastStateTime = gameTime;
    state = STRUCT_ANIM_STATES::OPENING;
    break;
  case STRUCT_ANIM_STATES::OPEN:
    lastStateTime = gameTime;
    return 0;  // Already open.
  case STRUCT_ANIM_STATES::OPENING:
    break;
  case STRUCT_ANIM_STATES::CLOSING:
    lastStateTime = 2 * gameTime - lastStateTime - SAS_OPEN_SPEED;
    state = STRUCT_ANIM_STATES::OPENING;
    return 0; // Busy
  }

  return lastStateTime + SAS_OPEN_SPEED - gameTime;
}

int Structure::gateCurrentOpenHeight(uint32_t time, int minimumStub) const
{
  StructureStats const *psStructureStats = stats;
  if (psStructureStats->type == REF_GATE)
  {
    int height = displayData.imd->max.y;
    int openHeight;
    switch (state)
    {
    case SAS_OPEN:
      openHeight = height;
      break;
    case SAS_OPENING:
      openHeight = (height * std::max<int>(time + GAME_TICKS_PER_UPDATE - lastStateTime, 0)) / SAS_OPEN_SPEED;
      break;
    case SAS_CLOSING:
      openHeight = height - (height * std::max<int>(time - lastStateTime, 0)) / SAS_OPEN_SPEED;
      break;
    default:
      return 0;
    }
    return std::max(std::min(openHeight, height - minimumStub), 0);
  }
  return 0;
}

void Structure::aiUpdateStructure(bool isMission)
{
  UDWORD				structureMode = 0;
  Droid *psDroid;
  GameObject *psChosenObjs[MAX_WEAPONS] = {nullptr};
  GameObject *psChosenObj = nullptr;
  Factory *psFactory;
  RepairFacility *psRepairFac = nullptr;
  Vector3i iVecEffect;
  bool				bDroidPlaced = false;
  WEAPON_STATS		*psWStats;
  bool				bDirect = false;
  SDWORD				xdiff, ydiff, mindist, currdist;
  TARGET_ORIGIN tmpOrigin = ORIGIN_UNKNOWN;

  CHECK_STRUCTURE(psStructure);

  if (time == gameTime)
  {
    // This isn't supposed to happen, and really shouldn't be possible - if this happens, maybe a structure is being updated twice?
    int count1 = 0, count2 = 0;
    Structure *s;
    for (s =         apsStructLists[owningPlayer]; s != nullptr; s = s->psNext)
    {
      count1 += s == psStructure;
    }
    for (s = mission.apsStructLists[owningPlayer]; s != nullptr; s = s->psNext)
    {
      count2 += s == psStructure;
    }
    debug(LOG_ERROR, "prevTime = %u, time = %u, gameTime = %u, count1 = %d, count2 = %d", prevTime, time, gameTime, count1, count2);
    --time;
  }
  prevTime = time;
  time = gameTime;
  for (UDWORD i = 0; i < MAX(1, numWeapons); ++i)
  {
    m_weaponList[i].prevRot = m_weaponList[i].rot;
  }

  if (isMission)
  {
    switch (stats->type)
    {
    case REF_RESEARCH:
    case REF_FACTORY:
    case REF_CYBORG_FACTORY:
    case REF_VTOL_FACTORY:
      break;
    default:
      return; // nothing to do
    }
  }

  // Will go out into a building EVENT stats/text file
  /* Spin round yer sensors! */
  if (numWeapons == 0)
  {
    if ((m_weaponList[0].nStat == 0) &&
        (stats->type != REF_REPAIR_FACILITY))
    {

      //////
      // - radar should rotate every three seconds ... 'cause we timed it at Heathrow !
      // gameTime is in milliseconds - one rotation every 3 seconds = 1 rotation event 3000 millisecs
      m_weaponList[0].rot.direction = (uint16_t)((uint64_t)gameTime * 65536 / 3000) + ((getPosition.x +
                                                                                                     getPosition.y) % 10) * 6550;  // Randomize by hashing position as seed for rotating 1/10th turns. Cast wrapping intended.
      m_weaponList[0].rot.pitch = 0;
    }
  }

  /* Check lassat */
  if (isLasSat(stats)
      && gameTime - m_weaponList[0].lastFired > weaponFirePause(&asWeaponStats[m_weaponList[0].nStat], owningPlayer)
      && m_weaponList[0].ammo > 0)
  {
    triggerEventStructureReady(psStructure);
    m_weaponList[0].ammo = 0; // do not fire more than once
  }

  /* See if there is an enemy to attack */
  if (numWeapons > 0)
  {
    //structures always update their targets
    for (UDWORD i = 0; i < numWeapons; i++)
    {
      bDirect = proj_Direct(asWeaponStats + m_weaponList[i].nStat);
      if (m_weaponList[i].nStat > 0 &&
          asWeaponStats[m_weaponList[i].nStat].weaponSubClass != WSC_LAS_SAT)
      {
        if (aiChooseTarget(psStructure, &psChosenObjs[i], i, true, &tmpOrigin))
        {
          objTrace(id, "Weapon %d is targeting %d at (%d, %d)", i, psChosenObjs[i]->id,
                   psChosenObjs[i]->getPosition.x, psChosenObjs[i]->getPosition.y);
          setStructureTarget(psStructure, psChosenObjs[i], i, tmpOrigin);
        }
        else
        {
          if (aiChooseTarget(psStructure, &psChosenObjs[0], 0, true, &tmpOrigin))
          {
            if (psChosenObjs[0])
            {
              objTrace(id, "Weapon %d is supporting main weapon: %d at (%d, %d)", i,
                       psChosenObjs[0]->id, psChosenObjs[0]->getPosition.x, psChosenObjs[0]->getPosition.y);
              setStructureTarget(psStructure, psChosenObjs[0], i, tmpOrigin);
              psChosenObjs[i] = psChosenObjs[0];
            }
            else
            {
              setStructureTarget(psStructure, nullptr, i, ORIGIN_UNKNOWN);
              psChosenObjs[i] = nullptr;
            }
          }
          else
          {
            setStructureTarget(psStructure, nullptr, i, ORIGIN_UNKNOWN);
            psChosenObjs[i] = nullptr;
          }
        }

        if (psChosenObjs[i] != nullptr && !aiObjectIsProbablyDoomed(psChosenObjs[i], bDirect))
        {
          // get the weapon stat to see if there is a visible turret to rotate
          psWStats = asWeaponStats + m_weaponList[i].nStat;

          //if were going to shoot at something move the turret first then fire when locked on
          if (psWStats->pMountGraphic == nullptr)//no turret so lock on whatever
          {
            m_weaponList[i].rot.direction = calcDirection(
                getPosition.x,
                getPosition.y,
                psChosenObjs[i]->getPosition.x,
                psChosenObjs[i]->getPosition.y);
            combFire(&m_weaponList[i], psStructure, psChosenObjs[i], i);
          }
          else if (actionTargetTurret(psStructure, psChosenObjs[i], &m_weaponList
                                                                         [i]))
          {
            combFire(&m_weaponList[i], psStructure, psChosenObjs[i], i);
          }
        }
        else
        {
          // realign the turret
          if ((m_weaponList[i].rot.direction % DEG(90)) != 0 || m_weaponList[i].rot.pitch != 0)
          {
            actionAlignTurret(psStructure, i);
          }
        }
      }
    }
  }

  /* See if there is an enemy to attack for Sensor Towers that have weapon droids attached*/
  else if (stats->pSensor)
  {
    if (structStandardSensor(psStructure) || structVTOLSensor(psStructure) || objRadarDetector(psStructure))
    {
      if (aiChooseSensorTarget(psStructure, &psChosenObj))
      {
        objTrace(id, "Sensing (%d)", psChosenObj->id);
        if (objRadarDetector(psStructure))
        {
          setStructureTarget(psStructure, psChosenObj, 0, ORIGIN_RADAR_DETECTOR);
        }
        else
        {
          setStructureTarget(psStructure, psChosenObj, 0, ORIGIN_SENSOR);
        }
      }
      else
      {
        setStructureTarget(psStructure, nullptr, 0, ORIGIN_UNKNOWN);
      }
      psChosenObj = psTarget[0];
    }
    else
    {
      psChosenObj = psTarget[0];
    }
  }
  //only interested if the Structure "does" something!
  if (pFunctionality == nullptr)
  {
    return;
  }

  /* Process the functionality according to type
	* determine the subject stats (for research or manufacture)
	* or base object (for repair) or update power levels for resourceExtractor
   */
  StatsObject *pSubject = nullptr;
  switch (stats->type)
  {
  case REF_RESEARCH:
  {
    pSubject = pFunctionality->researchFacility.psSubject;
    structureMode = REF_RESEARCH;
    break;
  }
  case REF_FACTORY:
  case REF_CYBORG_FACTORY:
  case REF_VTOL_FACTORY:
  {
    pSubject = pFunctionality->factory.psSubject;
    structureMode = REF_FACTORY;
    //check here to see if the factory's commander has died
    if (pFunctionality->factory.psCommander &&
        pFunctionality->factory.psCommander->deathTime)
    {
      //remove the commander from the factory
      syncDebugDroid(pFunctionality->factory.psCommander, '-');
      assignFactoryCommandDroid(psStructure, nullptr);
    }
    break;
  }
  case REF_REPAIR_FACILITY: // FIXME FIXME FIXME: Magic numbers in this section
  {
    psRepairFac = &pFunctionality->repairFacility;
    psChosenObj = psRepairFac->psObj;
    structureMode = REF_REPAIR_FACILITY;
    psDroid = (Droid *)psChosenObj;

    // If the droid we're repairing just died, find a new one
    if (psDroid && psDroid->deathTime)
    {
      syncDebugDroid(psDroid, '-');
      psDroid = nullptr;
      psChosenObj = nullptr;
      psRepairFac->psObj = nullptr;
    }

    // skip droids that are trying to get to other repair factories
    if (psDroid != nullptr
        && (!orderState(psDroid, DORDER_RTR)
            || psDroid->order.psObj != psStructure))
    {
      psDroid = (Droid *)psChosenObj;
      xdiff = (SDWORD)psDroid->getPosition.x -
              (SDWORD)getPosition.x;
      ydiff = (SDWORD)psDroid->getPosition.y -
              (SDWORD)getPosition.y;
      // unless it has orders to repair here, forget about it when it gets out of range
      if (xdiff * xdiff + ydiff * ydiff > (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2))
      {
        psChosenObj = nullptr;
        psDroid = nullptr;
        psRepairFac->psObj = nullptr;
      }
    }

    // select next droid if none being repaired,
    // or look for a better droid if not repairing one with repair orders
    if (psChosenObj == nullptr ||
        (((Droid *)psChosenObj)->order.type != DORDER_RTR && ((Droid *)psChosenObj)->order.type != DORDER_RTR_SPECIFIED))
    {
      //FIX ME: (doesn't look like we need this?)
      ASSERT(psRepairFac->psGroup != nullptr, "invalid repair facility group pointer");

      // Tries to find most important droid to repair
      // Lower dist = more important
      // mindist contains lowest dist found so far
      mindist = (TILE_UNITS * 8) * (TILE_UNITS * 8) * 3;
      if (psChosenObj)
      {
        // We already have a valid droid to repair, no need to look at
        // droids without a repair order.
        mindist = (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2;
      }
      psRepairFac->droidQueue = 0;
      for (psDroid =
               allDroidLists[owningPlayer]; psDroid; psDroid = psDroid->psNext)
      {
        GameObject *const psTarget = orderStateObj(psDroid, DORDER_RTR);

        // Highest priority:
        // Take any droid with orders to Return to Repair (DORDER_RTR),
        // or that have been ordered to this repair facility (DORDER_RTR_SPECIFIED),
        // or any "lost" unit with one of those two orders.
        if (((psDroid->order.type == DORDER_RTR || (psDroid->order.type == DORDER_RTR_SPECIFIED
                                                    && (!psTarget || psTarget == psStructure)))
             && psDroid->action != DACTION_WAITFORREPAIR && psDroid->action != DACTION_MOVETOREPAIRPOINT
             && psDroid->action != DACTION_WAITDURINGREPAIR)
            || (psTarget && psTarget == psStructure))
        {
          if (psDroid->hitPoints >= psDroid->originalBody)
          {
            objTrace(id, "Repair not needed of droid %d", (int)psDroid->id);

            /* set droid points to max */
            psDroid->hitPoints = psDroid->originalBody;

            // if completely repaired reset order
            secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);

            if (hasCommander(psDroid))
            {
              // return a droid to it's command group
              Droid *psCommander = psDroid->psGroup->psCommander;

              orderDroidObj(psDroid, DORDER_GUARD, psCommander, ModeImmediate);
            }
            else if (psRepairFac->psDeliveryPoint != nullptr)
            {
              // move the droid out the way
              objTrace(psDroid->id, "Repair not needed - move to delivery point");
              orderDroidLoc(psDroid, DORDER_MOVE,
                            psRepairFac->psDeliveryPoint->coords.x,
                            psRepairFac->psDeliveryPoint->coords.y, ModeQueue);  // ModeQueue because delivery points are not yet synchronised!
            }
            continue;
          }
          xdiff =
              (SDWORD)
                  psDroid->getPosition.x -
              (SDWORD)psStructure
                  ->getPosition.x;
          ydiff =
              (SDWORD)
                  psDroid->getPosition.y -
              (SDWORD)psStructure
                  ->getPosition.y;
          currdist = xdiff * xdiff + ydiff * ydiff;
          if (currdist < mindist && currdist < (TILE_UNITS * 8) * (TILE_UNITS * 8))
          {
            mindist = currdist;
            psChosenObj = psDroid;
          }
          if (psTarget && psTarget == psStructure)
          {
            psRepairFac->droidQueue++;
          }
        }
        // Second highest priority:
        // Help out another nearby repair facility
        else if (psTarget && mindist > (TILE_UNITS * 8) * (TILE_UNITS * 8)
                 && psTarget != psStructure && psDroid->action == DACTION_WAITFORREPAIR)
        {
          int distLimit = mindist;
          if (psTarget->getType ==
                  OBJ_STRUCTURE && ((Structure *)psTarget)->stats->type == REF_REPAIR_FACILITY)  // Is a repair facility (not the HQ).
          {
            RepairFacility *stealFrom = &((Structure *)psTarget)->pFunctionality->repairFacility;
            // make a wild guess about what is a good distance
            distLimit = world_coord(stealFrom->droidQueue) * world_coord(stealFrom->droidQueue) * 10;
          }

          xdiff =
              (SDWORD)
                  psDroid->getPosition.x -
              (SDWORD)psStructure
                  ->getPosition.x;
          ydiff =
              (SDWORD)
                  psDroid->getPosition.y -
              (SDWORD)psStructure
                  ->getPosition.y;
          currdist = xdiff * xdiff + ydiff * ydiff + (TILE_UNITS * 8) * (TILE_UNITS * 8); // lower priority
          if (currdist < mindist && currdist - (TILE_UNITS * 8) * (TILE_UNITS * 8) < distLimit)
          {
            mindist = currdist;
            psChosenObj = psDroid;
            psRepairFac->droidQueue++;	// shared queue
            objTrace(psChosenObj->id, "Stolen by another repair facility, currdist=%d, mindist=%d, distLimit=%d", (int)currdist, (int)mindist, distLimit);
          }
        }
        // Lowest priority:
        // Just repair whatever is nearby and needs repairing.
        else if (mindist > (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2 && psDroid->hitPoints < psDroid->originalBody)
        {
          xdiff =
              (SDWORD)psDroid->getPosition.x -
              (SDWORD)
                  getPosition.x;
          ydiff =
              (SDWORD)
                  psDroid->getPosition.y -
              (SDWORD)psStructure
                  ->getPosition.y;
          currdist = xdiff * xdiff + ydiff * ydiff + (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2; // even lower priority
          if (currdist < mindist && currdist < (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2) + (TILE_UNITS * 8) * (TILE_UNITS * 8) * 2)
          {
            mindist = currdist;
            psChosenObj = psDroid;
          }
        }
      }
      if (!psChosenObj) // Nothing to repair? Repair allied units!
      {
        mindist = (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2);

        for (uint8_t i = 0; i < MAX_PLAYERS; i++)
        {
          if (aiCheckAlliances(i, owningPlayer) && i != owningPlayer)
          {
            for (psDroid =
                     allDroidLists[i]; psDroid; psDroid = psDroid->psNext)
            {
              if (psDroid->hitPoints < psDroid->originalBody)
              {
                xdiff =
                    (SDWORD)psDroid
                        ->getPosition
                        .x -
                    (SDWORD)psStructure
                        ->getPosition
                        .x;
                ydiff =
                    (SDWORD)psDroid
                        ->getPosition
                        .y -
                    (SDWORD)psStructure
                        ->getPosition
                        .y;
                currdist = xdiff * xdiff + ydiff * ydiff;
                if (currdist < mindist)
                {
                  mindist = currdist;
                  psChosenObj = psDroid;
                }
              }
            }
          }
        }
      }
      psDroid = (Droid *)psChosenObj;
      if (psDroid)
      {
        if (psDroid->order.type == DORDER_RTR || psDroid->order.type == DORDER_RTR_SPECIFIED)
        {
          // Hey, droid, it's your turn! Stop what you're doing and get ready to get repaired!
          psDroid->action = DACTION_WAITFORREPAIR;
          psDroid->order.psObj = psStructure;
        }
        objTrace(id, "Chose to repair droid %d", (int)psDroid->id);
        objTrace(psDroid->id, "Chosen to be repaired by repair structure %d", (int)id);
      }
    }

    // send the droid to be repaired
    if (psDroid)
    {
      /* set chosen object */
      psChosenObj = psDroid;

      /* move droid to repair point at rear of facility */
      xdiff = (SDWORD)psDroid->getPosition.x -
              (SDWORD)getPosition.x;
      ydiff = (SDWORD)psDroid->getPosition.y -
              (SDWORD)getPosition.y;
      if (psDroid->action == DACTION_WAITFORREPAIR ||
          (psDroid->action == DACTION_WAITDURINGREPAIR
           && xdiff * xdiff + ydiff * ydiff > (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2)))
      {
        objTrace(id, "Requesting droid %d to come to us", (int)psDroid->id);
        actionDroid(psDroid,
                    DACTION_MOVETOREPAIRPOINT,
                    psStructure,
                    getPosition.x,
                    getPosition.y);
      }
      /* reset repair started if we were previously repairing something else */
      if (psRepairFac->psObj != psDroid)
      {
        psRepairFac->psObj = psDroid;
      }
    }

    // update repair arm position
    if (psChosenObj)
    {
      actionTargetTurret(psStructure, psChosenObj, &m_weaponList[0]);
    }
    else if ((m_weaponList[0].rot.direction % DEG(90)) != 0 || m_weaponList[0].rot.pitch != 0)
    {
      // realign the turret
      actionAlignTurret(psStructure, 0);
    }

    break;
  }
  case REF_REARM_PAD:
  {
    RearmPad *psReArmPad = &pFunctionality->rearmPad;

    psChosenObj = psReArmPad->psObj;
    structureMode = REF_REARM_PAD;
    psDroid = nullptr;

    /* select next droid if none being rearmed*/
    if (psChosenObj == nullptr)
    {
      objTrace(id, "Rearm pad idle - look for victim");
      for (psDroid =
               allDroidLists[owningPlayer]; psDroid; psDroid = psDroid->psNext)
      {
        // move next droid waiting on ground to rearm pad
        if (vtolReadyToRearm(psDroid, psStructure) &&
            (psChosenObj == nullptr || (((Droid *)psChosenObj)->actionStarted > psDroid->actionStarted)))
        {
          objTrace(psDroid->id, "rearm pad candidate");
          objTrace(id, "we found %s to rearm", objInfo(psDroid));
          psChosenObj = psDroid;
        }
      }
      // None available? Try allies.
      for (int i = 0; i < MAX_PLAYERS && !psChosenObj; i++)
      {
        if (aiCheckAlliances(i, owningPlayer) && i != owningPlayer)
        {
          for (psDroid = allDroidLists[i]; psDroid; psDroid = psDroid->psNext)
          {
            // move next droid waiting on ground to rearm pad
            if (vtolReadyToRearm(psDroid, psStructure))
            {
              psChosenObj = psDroid;
              objTrace(psDroid->id, "allied rearm pad candidate");
              objTrace(id, "we found allied %s to rearm", objInfo(psDroid));
              break;
            }
          }
        }
      }
      psDroid = (Droid *)psChosenObj;
      if (psDroid != nullptr)
      {
        actionDroid(psDroid, DACTION_MOVETOREARMPOINT, psStructure);
      }
    }
    else
    {
      psDroid = (Droid *) psChosenObj;
      if ((psDroid->sMove.Status == MOVEINACTIVE ||
           psDroid->sMove.Status == MOVEHOVER) &&
          psDroid->action == DACTION_WAITFORREARM)
      {
        objTrace(psDroid->id, "supposed to go to rearm but not on our way -- fixing"); // this should never happen...
        actionDroid(psDroid, DACTION_MOVETOREARMPOINT, psStructure);
      }
    }

    // if found a droid to rearm assign it to the rearm pad
    if (psDroid != nullptr)
    {
      /* set chosen object */
      psChosenObj = psDroid;
      psReArmPad->psObj = psChosenObj;
      if (psDroid->action == DACTION_MOVETOREARMPOINT)
      {
        /* reset rearm started */
        psReArmPad->timeStarted = ACTION_START_TIME;
        psReArmPad->timeLastUpdated = 0;
      }
      auxStructureBlocking(psStructure);
    }
    else
    {
      auxStructureNonblocking(psStructure);
    }
    break;
  }
  default:
    break;
  }

  /* check subject stats (for research or manufacture) */
  if (pSubject != nullptr)
  {
    //if subject is research...
    if (structureMode == REF_RESEARCH)
    {
      RESEARCH_FACILITY *psResFacility = &pFunctionality->researchFacility;

      //if on hold don't do anything
      if (psResFacility->timeStartHold)
      {
        delPowerRequest(psStructure);
        return;
      }

      //electronic warfare affects the functionality of some structures in multiPlayer
      if (bMultiPlayer && resistance < (int)structureResistance(stats, owningPlayer))
      {
        return;
      }

      int researchIndex = pSubject->id - STAT_RESEARCH;

      PLAYER_RESEARCH *pPlayerRes = &asPlayerResList[owningPlayer][researchIndex];
      //check research has not already been completed by another structure
      if (!IsResearchCompleted(pPlayerRes))
      {
        RESEARCH *pResearch = (RESEARCH *)pSubject;

        unsigned pointsToAdd = gameTimeAdjustedAverage(getBuildingResearchPoints(psStructure));
        pointsToAdd = MIN(pointsToAdd, pResearch->researchPoints - pPlayerRes->currentPoints);

        unsigned shareProgress = pPlayerRes->currentPoints;  // Share old research progress instead of new one, so it doesn't get sped up by multiple players researching.
        bool shareIsFinished = false;

        if (pointsToAdd > 0 && pPlayerRes->currentPoints == 0)
        {
          bool haveEnoughPower = requestPowerFor(psStructure, pResearch->researchPower);
          if (haveEnoughPower)
          {
            shareProgress = 1;  // Share research payment, to avoid double payment even if starting research in the same game tick.
          }
          else
          {
            pointsToAdd = 0;
          }
        }

        if (pointsToAdd > 0 && pResearch->researchPoints > 0)  // might be a "free" research
        {
          pPlayerRes->currentPoints += pointsToAdd;
        }
        syncDebug("Research at %u/%u.", pPlayerRes->currentPoints, pResearch->researchPoints);

        //check if Research is complete
        if (pPlayerRes->currentPoints >= pResearch->researchPoints)
        {
          int prevState = intGetResearchState();

          //store the last topic researched - if its the best
          if (psResFacility->psBestTopic == nullptr)
          {
            psResFacility->psBestTopic = psResFacility->psSubject;
          }
          else
          {
            if (pResearch->researchPoints > psResFacility->psBestTopic->researchPoints)
            {
              psResFacility->psBestTopic = psResFacility->psSubject;
            }
          }
          psResFacility->psSubject = nullptr;
          intResearchFinished(psStructure);
          researchResult(researchIndex, owningPlayer, true, psStructure, true);

          shareIsFinished = true;

          //check if this result has enabled another topic
          intNotifyResearchButton(prevState);
        }

        // Update allies research accordingly
        if (game.type == LEVEL_TYPE::SKIRMISH && alliancesSharedResearch(game.alliance))
        {
          for (uint8_t i = 0; i < MAX_PLAYERS; i++)
          {
            if (alliances[i][owningPlayer] == ALLIANCE_FORMED)
            {
              if (!IsResearchCompleted(&asPlayerResList[i][researchIndex]))
              {
                // Share the research for that player.
                auto &allyProgress = asPlayerResList[i][researchIndex].currentPoints;
                allyProgress = std::max(allyProgress, shareProgress);
                if (shareIsFinished)
                {
                  researchResult(researchIndex, i, false, nullptr, true);
                }
              }
            }
          }
        }
      }
      else
      {
        //cancel this Structure's research since now complete
        psResFacility->psSubject = nullptr;
        intResearchFinished(psStructure);
        syncDebug("Research completed elsewhere.");
      }
    }
    //check for manufacture
    else if (structureMode == REF_FACTORY)
    {
      psFactory = &pFunctionality->factory;

      //if on hold don't do anything
      if (psFactory->timeStartHold)
      {
        return;
      }

      //electronic warfare affects the functionality of some structures in multiPlayer
      if (bMultiPlayer && resistance < (int)structureResistance(stats, owningPlayer))
      {
        return;
      }

      if (psFactory->timeStarted == ACTION_START_TIME)
      {
        // also need to check if a command droid's group is full

        // If the factory commanders group is full - return
        if (IsFactoryCommanderGroupFull(psFactory) || checkHaltOnMaxUnitsReached(psStructure, isMission))
        {
          return;
        }

        //set the time started
        psFactory->timeStarted = gameTime;
      }

      if (psFactory->buildPointsRemaining > 0)
      {
        int progress = gameTimeAdjustedAverage(getBuildingProductionPoints(psStructure));
        if ((unsigned)psFactory->buildPointsRemaining == calcTemplateBuild(psFactory->psSubject) && progress > 0)
        {
          // We're just starting to build, check for power.
          bool haveEnoughPower = requestPowerFor(psStructure, calcTemplatePower(psFactory->psSubject));
          if (!haveEnoughPower)
          {
            progress = 0;
          }
        }
        psFactory->buildPointsRemaining -= progress;
      }

      //check for manufacture to be complete
      if (psFactory->buildPointsRemaining <= 0 && !IsFactoryCommanderGroupFull(psFactory) && !checkHaltOnMaxUnitsReached(psStructure, isMission))
      {
        if (isMission)
        {
          // put it in the mission list
          psDroid = buildMissionDroid(
              (DroidStats *)pSubject,
              getPosition.x,
              getPosition.y,
              owningPlayer);
          if (psDroid)
          {
            psDroid->secondaryOrder = psFactory->secondaryOrder;
            psDroid->secondaryOrderPending = psDroid->secondaryOrder;
            setFactorySecondaryState(psDroid, psStructure);
            setDroidBase(psDroid, psStructure);
            bDroidPlaced = true;
          }
        }
        else
        {
          // place it on the map
          bDroidPlaced = structPlaceDroid(psStructure, (DroidStats *)pSubject, &psDroid);
        }

        //script callback, must be called after factory was flagged as idle
        if (bDroidPlaced)
        {
          //reset the start time
          psFactory->timeStarted = ACTION_START_TIME;
          psFactory->psSubject = nullptr;

          doNextProduction(psStructure, (DroidStats *)pSubject, ModeImmediate);

          cbNewDroid(psStructure, psDroid);
        }
      }
    }
  }

  /* check base object (for repair / rearm) */
  if (psChosenObj != nullptr)
  {
    if (structureMode == REF_REPAIR_FACILITY)
    {
      psDroid = (Droid *) psChosenObj;
      ASSERT_OR_RETURN(, psDroid != nullptr, "invalid droid pointer");
      psRepairFac = &pFunctionality->repairFacility;

      xdiff = (SDWORD)psDroid->getPosition.x -
              (SDWORD)getPosition.x;
      ydiff = (SDWORD)psDroid->getPosition.y -
              (SDWORD)getPosition.y;
      if (xdiff * xdiff + ydiff * ydiff <= (TILE_UNITS * 5 / 2) * (TILE_UNITS * 5 / 2))
      {
        //check droid is not healthy
        if (psDroid->hitPoints < psDroid->originalBody)
        {
          //if in multiPlayer, and a Transporter - make sure its on the ground before repairing
          if (bMultiPlayer && isTransporter(psDroid))
          {
            if (!(psDroid->sMove.Status == MOVEINACTIVE &&
                  psDroid->sMove.iVertSpeed == 0))
            {
              objTrace(id, "Waiting for transporter to land");
              return;
            }
          }

          //don't do anything if the resistance is low in multiplayer
          if (bMultiPlayer && resistance < (int)structureResistance(stats, owningPlayer))
          {
            objTrace(id, "Resistance too low for repair");
            return;
          }

          psDroid->hitPoints += gameTimeAdjustedAverage(getBuildingRepairPoints(psStructure));
        }

        if (psDroid->hitPoints >= psDroid->originalBody)
        {
          objTrace(id, "Repair complete of droid %d", (int)psDroid->id);

          psRepairFac->psObj = nullptr;

          /* set droid points to max */
          psDroid->hitPoints = psDroid->originalBody;

          if ((psDroid->order.type == DORDER_RTR || psDroid->order.type == DORDER_RTR_SPECIFIED)
              && psDroid->order.psObj == psStructure)
          {
            // if completely repaired reset order
            secondarySetState(psDroid, DSO_RETURN_TO_LOC, DSS_NONE);

            if (hasCommander(psDroid))
            {
              // return a droid to it's command group
              Droid *psCommander = psDroid->psGroup->psCommander;

              objTrace(psDroid->id, "Repair complete - move to commander");
              orderDroidObj(psDroid, DORDER_GUARD, psCommander, ModeImmediate);
            }
            else if (psRepairFac->psDeliveryPoint != nullptr)
            {
              // move the droid out the way
              objTrace(psDroid->id, "Repair complete - move to delivery point");
              orderDroidLoc(psDroid, DORDER_MOVE,
                            psRepairFac->psDeliveryPoint->coords.x,
                            psRepairFac->psDeliveryPoint->coords.y, ModeQueue);  // ModeQueue because delivery points are not yet synchronised!
            }
          }
        }

        if (visibleForLocalDisplay() && psDroid->visibleForLocalDisplay()) // display only - does not impact simulation state
        {
          /* add plasma repair effect whilst being repaired */
          iVecEffect.x = psDroid->getPosition.x +
                         (10 - rand() % 20);
          iVecEffect.y = psDroid->getPosition.z +
                         (10 - rand() % 20);
          iVecEffect.z = psDroid->getPosition.y +
                         (10 - rand() % 20);
          effectSetSize(100);
          addEffect(&iVecEffect, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED, true, getImdFromIndex(MI_FLAME), 0, gameTime - deltaGameTime + 1);
        }
      }
    }
    //check for rearming
    else if (structureMode == REF_REARM_PAD)
    {
      RearmPad *psReArmPad = &pFunctionality->rearmPad;
      UDWORD pointsAlreadyAdded;

      psDroid = (Droid *)psChosenObj;
      ASSERT_OR_RETURN(, psDroid != nullptr, "invalid droid pointer");
      ASSERT_OR_RETURN(, isVtolDroid(psDroid), "invalid droid getType");

      //check hasn't died whilst waiting to be rearmed
      // also clear out any previously repaired droid
      if (psDroid->deathTime || (psDroid->action != DACTION_MOVETOREARMPOINT && psDroid->action != DACTION_WAITDURINGREARM))
      {
        syncDebugDroid(psDroid, '-');
        psReArmPad->psObj = nullptr;
        objTrace(psDroid->id, "VTOL has wrong action or is dead");
        return;
      }
      if (psDroid->action == DACTION_WAITDURINGREARM && psDroid->sMove.Status == MOVEINACTIVE)
      {
        if (psReArmPad->timeStarted == ACTION_START_TIME)
        {
          //set the time started and last updated
          psReArmPad->timeStarted = gameTime;
          psReArmPad->timeLastUpdated = gameTime;
        }
        unsigned pointsToAdd = getBuildingRearmPoints(psStructure) * (gameTime - psReArmPad->timeStarted) / GAME_TICKS_PER_SEC;
        pointsAlreadyAdded = getBuildingRearmPoints(psStructure) * (psReArmPad->timeLastUpdated - psReArmPad->timeStarted) / GAME_TICKS_PER_SEC;
        if (pointsToAdd >= psDroid->weight) // amount required is a factor of the droid weight
        {
          // We should be fully loaded by now.
          for (unsigned i = 0; i < psDroid->numWeapons; i++)
          {
            // set rearm value to no runs made
            psDroid->m_weaponList[i].usedAmmo = 0;
            psDroid->m_weaponList[i].ammo = asWeaponStats[psDroid->m_weaponList
                                                              [i].nStat].upgrade[psDroid->owningPlayer].numRounds;
            psDroid->m_weaponList[i].lastFired = 0;
          }
          objTrace(psDroid->id, "fully loaded");
        }
        else
        {
          for (unsigned i = 0; i < psDroid->numWeapons; i++)		// rearm one weapon at a time
          {
            // Make sure it's a rearmable weapon (and so we don't divide by zero)
            if (psDroid->m_weaponList[i].usedAmmo > 0 && asWeaponStats[psDroid->m_weaponList
                                                                           [i].nStat].upgrade[psDroid->owningPlayer].numRounds > 0)
            {
              // Do not "simplify" this formula.
              // It is written this way to prevent rounding errors.
              int ammoToAddThisTime =
                  pointsToAdd * getNumAttackRuns(psDroid, i) / psDroid->weight -
                  pointsAlreadyAdded * getNumAttackRuns(psDroid, i) / psDroid->weight;
              psDroid->m_weaponList[i].usedAmmo -= std::min<unsigned>(ammoToAddThisTime, psDroid->m_weaponList
                                                                                             [i].usedAmmo);
              if (ammoToAddThisTime)
              {
                // reset ammo and lastFired
                psDroid->m_weaponList
                    [i].ammo = asWeaponStats[psDroid->m_weaponList
                                                  [i].nStat].upgrade[psDroid->owningPlayer].numRounds;
                psDroid->m_weaponList
                    [i].lastFired = 0;
                break;
              }
            }
          }
        }
        if (psDroid->hitPoints < psDroid->originalBody) // do repairs
        {
          psDroid->hitPoints += gameTimeAdjustedAverage(getBuildingRepairPoints(psStructure));
          if (psDroid->hitPoints >= psDroid->originalBody)
          {
            psDroid->hitPoints = psDroid->originalBody;
          }
        }
        psReArmPad->timeLastUpdated = gameTime;

        //check for fully armed and fully repaired
        if (vtolHappy(psDroid))
        {
          //clear the rearm pad
          psDroid->action = DACTION_NONE;
          psReArmPad->psObj = nullptr;
          auxStructureNonblocking(psStructure);
          triggerEventDroidIdle(psDroid);
          objTrace(psDroid->id, "VTOL happy and ready for action!");
        }
      }
    }
  }
}

//print some info at the top of the screen dependent on the structure
void Structure::printStructureInfo()
{
  unsigned int numConnected;
  POWER_GEN	*psPowerGen;

  if (isBlueprint(psStructure))
  {
    return;  // Don't print anything about imaginary structures. Would crash, anyway.
  }

  const DebugInputManager& dbgInputManager = gInputManager.debugManager();
  switch (stats->type)
  {
  case REF_HQ:
  {
    unsigned int assigned_droids = countAssignedDroids(psStructure);
    console(ngettext("%s - %u Unit assigned - Hitpoints %d/%d", "%s - %u Units assigned - Hitpoints %d/%d", assigned_droids),
            getStatsName(stats), assigned_droids, hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %d - sensor range %d - ECM %d"), id, structSensorRange(psStructure), structJammerPower(psStructure));
    }
    break;
  }
  case REF_DEFENSE:
    if (stats->pSensor != nullptr
        && (stats->pSensor->type == STANDARD_SENSOR
            || stats->pSensor->type == INDIRECT_CB_SENSOR
            || stats->pSensor->type == VTOL_INTERCEPT_SENSOR
            || stats->pSensor->type == VTOL_CB_SENSOR
            || stats->pSensor->type == SUPER_SENSOR
            || stats->pSensor->type == RADAR_DETECTOR_SENSOR)
        && stats->pSensor->location == LOC_TURRET)
    {
      unsigned int assigned_droids = countAssignedDroids(psStructure);
      console(ngettext("%s - %u Unit assigned - Damage %d/%d", "%s - %u Units assigned - Hitpoints %d/%d", assigned_droids),
              getStatsName(stats), assigned_droids, hitPoints, structureBody(psStructure));
    }
    else
    {
      console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    }
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %d - armour %d|%d - sensor range %d - ECM %d - born %u - depth %.02f"),
              id, objArmour(psStructure, WC_KINETIC), objArmour(psStructure, WC_HEAT),
              structSensorRange(psStructure), structJammerPower(psStructure), creationTime, foundationDepth);
    }
    break;
  case REF_REPAIR_FACILITY:
    console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %d - Queue %d"), id, pFunctionality->repairFacility.droidQueue);
    }
    break;
  case REF_RESOURCE_EXTRACTOR:
    console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed() && selectedPlayer < MAX_PLAYERS)
    {
      console(_("ID %d - %s"), id, (auxTile(map_coord(position.x),
                                                         map_coord(position.y), selectedPlayer) & AUXBITS_DANGER) ? "danger" : "safe");
    }
    break;
  case REF_POWER_GEN:
    psPowerGen = &pFunctionality->powerGenerator;
    numConnected = 0;
    for (auto& apResExtractor : psPowerGen->apResExtractors)
    {
      if (apResExtractor)
      {
        numConnected++;
      }
    }
    console(_("%s - Connected %u of %u - Hitpoints %d/%d"), getStatsName(stats), numConnected,
            NUM_POWER_MODULES, hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %u - Multiplier: %u"), id, getBuildingPowerPoints(psStructure));
    }
    break;
  case REF_CYBORG_FACTORY:
  case REF_VTOL_FACTORY:
  case REF_FACTORY:
    console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %u - Production Output: %u - BuildPointsRemaining: %u - Resistance: %d / %d"), id,
              getBuildingProductionPoints(psStructure), pFunctionality->factory.buildPointsRemaining,
              resistance, structureResistance(stats, owningPlayer));
    }
    break;
  case REF_RESEARCH:
    console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %u - Research Points: %u"), id, getBuildingResearchPoints(psStructure));
    }
    break;
  case REF_REARM_PAD:
    console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("tile %d,%d - target %s"),
              position.x / TILE_UNITS,
              position.y / TILE_UNITS,
              objInfo(pFunctionality->rearmPad.psObj));
    }
    break;
  default:
    console(_("%s - Hitpoints %d/%d"), getStatsName(stats), hitPoints, structureBody(psStructure));
    if (dbgInputManager.debugMappingsAllowed())
    {
      console(_("ID %u - sensor range %d - ECM %d"), id, structSensorRange(psStructure), structJammerPower(psStructure));
    }
    break;
  }
}