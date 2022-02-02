//
// Created by Luna Nothard on 01/02/2022.
//

#include "lib/framework/frame.h"

#include "droid.h"
#include "player.h"
#include "structure.h"
#include "objmem.h"

Player::Player(unsigned id)
  : id{id}
{
}

void Player::addDroid(unsigned droidId, unsigned player)
{
  droids.emplace_back(droidId, player);
}

void Player::addDroid(Droid& droid)
{
  droids.push_back(droid);
  if (droid.getType() == DROID_TYPE::SENSOR) {
    apsSensorList.push_back(&droid);
  }
}

Droid* Player::findDroidById(unsigned droidId) const
{
  return findById(droidId, droids);
}

void Player::addStructure(unsigned structId, unsigned player)
{
  structures.emplace_back(structId, player);
}

void Player::addStructure(Structure& structure)
{
  structures.push_back(structure);

  if (auto extr = dynamic_cast<ResourceExtractor*>(&structure)) {
    extractors.push_back(*extr);
  }

  auto sensorStats = structure.getStats()->sensor_stats;
  if (sensorStats && sensorStats->location == LOC::TURRET) {
    apsSensorList.push_back(&structure);
  }
}

void Player::killStructure(Structure& structure)
{
  structure.damageManager->setTimeOfDeath(gameTime);

  if (auto extr = dynamic_cast<ResourceExtractor*>(&structure)) {
    std::erase(extractors, *extr);
  }

  auto sensorStats = structure.getStats()->sensor_stats;
  if (sensorStats && sensorStats->location == LOC::TURRET) {
    std::erase(apsSensorList, &structure);
  }
}

Structure* Player::findStructureById(unsigned structId) const
{
  return findById(structId, structures);
}

void Player::setPlayer(unsigned playerId)
{
  id = playerId;
}

unsigned Player::getPlayer() const
{
  return id;
}

bool Player::isSelectedPlayer() const
{
  return id == selectedPlayer;
}

void killDroid(Droid& droid)
{
  droid.setBase(nullptr);
  droid.damageManager->setTimeOfDeath(gameTime);
  if (droid.getType() == DROID_TYPE::SENSOR) {
    std::erase(apsSensorList, &droid);
  }
}
