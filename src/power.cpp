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
 * @file power.cpp
 * Store PlayerPower and other power related stuff
 */

#include <fmt/core.h>

#include "mission.h"
#include "multiint.h"
#include "objmem.h"
#include "power.h"

bool offWorldKeepLists;


static constexpr auto EXTRACT_POINTS = 1;
static constexpr auto MAX_POWER = 1000000;

//flag used to check for power calculations to be done or not
bool powerCalculated;

/* Updates the current power based on the extracted power and a Power Generator*/
static int64_t updateExtractedPower(Structure* psBuilding);

//returns the relevant list based on OffWorld or OnWorld
static std::vector<std::unique_ptr<Structure>>& powerStructList(unsigned player);


PowerRequest::PowerRequest(unsigned id, int64_t amount)
  : id{id}, amount{amount}
{
}

//void setPowerModifier(unsigned player, int modifier)
//{
//	asPower[player].powerModifier = modifier;
//}

void setPowerMaxStorage(unsigned player, int64_t max)
{
	asPower[player].maxStorage = max;
	asPower[player].currentPower = std::min<int64_t>(asPower[player].maxStorage, asPower[player].currentPower);
}

/*allocate the space for the playerPower*/
bool allocPlayerPower()
{
	clearPlayerPower();
	powerCalculated = true;
	return true;
}

void clearPlayerPower()
{
  std::for_each(asPower.begin(), asPower.end(),
                [](auto& player_power) {
      player_power.currentPower = 0;
      player_power.extractedPower = 0;
      player_power.wastedPower = 0;
      player_power.powerModifier = 100;
      player_power.powerQueue.clear();
      player_power.maxStorage = MAX_POWER;
      player_power.powerGeneratedLastUpdate = 0;
  });
}

bool addPowerRequest(unsigned player, unsigned requester_id, int64_t amount)
{
  auto& player_power = asPower[player];
  auto required_power = amount;

  auto it = player_power.powerQueue.begin();
  for (; it->id != requester_id; ++it)
  {
    required_power += it->amount;
  }

  if (it == player_power.powerQueue.end()) {
    player_power.powerQueue.emplace_back(requester_id, amount);
  }
  else {
    it->amount = amount;
  }
  return required_power <= asPower[player].currentPower;
}

void removePowerRequest(Structure const& structure)
{
  auto& player_power = asPower[structure.playerManager->getPlayer()];
  std::erase_if(player_power.powerQueue, [&structure](auto& request) {
      return request.requester_id == structure.getId();
  });
}

static int64_t checkPrecisePowerRequest(Structure* psStruct)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(-1, psStruct);
	auto const p = &asPower[psStruct->playerManager->getPlayer()];

	int64_t requiredPower = 0;
	for (auto n = 0; n < p->powerQueue.size(); ++n)
	{
		requiredPower += p->powerQueue[n].amount;
    if (p->powerQueue[n].id != psStruct->getId()) {
      continue;
    }
    if (requiredPower <= p->currentPower) {
      return -1; // Have enough power.
    }
    return requiredPower - p->currentPower;
  }
	return -1;
}

int64_t checkPowerRequest(Structure* psStruct)
{
	return checkPrecisePowerRequest(psStruct);
}

static int64_t getPreciseQueuedPower(unsigned player)
{
	auto const* p = &asPower[player];
	int64_t requiredPower = 0;
	for (auto n : p->powerQueue)
	{
		requiredPower += n.amount;
	}
	return requiredPower;
}

int64_t getQueuedPower(unsigned player)
{
  auto const& queue = asPower[player].powerQueue;
  return std::accumulate(queue.begin(), queue.end(), 0,
                         [](int sum, auto const& request) {
      return sum + request.amount;
  });
}

static void syncDebugEconomy(unsigned player, char ch)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Bad player (%d)", player);

	syncDebug("%c economy%u = %" PRId64"", ch, player, asPower[player].currentPower);
}

void usePower(unsigned player, int64_t amount)
{
  ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Invalid player (%d)", player);
  asPower[player].currentPower = MAX(0, asPower[player].currentPower - amount);
}

void addPower(unsigned player, int64_t amount)
{
  ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Bad player (%d)", player);
  asPower[player].currentPower += amount;
  if (asPower[player].currentPower > asPower[player].maxStorage) {
    asPower[player].wastedPower += asPower[player].currentPower - asPower[player].maxStorage;
    asPower[player].currentPower = asPower[player].maxStorage;
  }
}

/*resets the power calc flag for all players*/
void powerCalc(bool on)
{
	powerCalculated = on;
}

/** Each Resource Extractor yields EXTRACT_POINTS per second FOREVER */
//static int64_t updateExtractedPower(STRUCTURE* psBuilding)
//{
//	RES_EXTRACTOR* pResExtractor;
//	int64_t extractedPoints;
//
//	pResExtractor = (RES_EXTRACTOR*)psBuilding->pFunctionality;
//	extractedPoints = 0;
//
//	//only extracts points whilst its active ie associated with a power gen
//	//and has got some power to extract
//	if (pResExtractor->psPowerGen != nullptr)
//	{
//		// include modifier as a %
//		extractedPoints = asPower[psBuilding->player].powerModifier * EXTRACT_POINTS * FP_ONE / (100 *
//			GAME_UPDATES_PER_SEC);
//		syncDebug("updateExtractedPower%d = %" PRId64"", psBuilding->player, extractedPoints);
//	}
//	ASSERT(extractedPoints >= 0, "extracted negative amount of power");
//	return extractedPoints;
//}

//returns the relevant list based on OffWorld or OnWorld
std::vector<std::unique_ptr<Structure>>& powerStructList(unsigned player)
{
	if (offWorldKeepLists) {
		return mission.apsStructLists[player];
	}
	else {
		return apsStructLists[player];
	}
}

/* Update current power based on what Power Generators exist */
void updatePlayerPower(unsigned player, int ticks)
{
  ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Invalid player %d", player);
	auto powerBefore = asPower[player].currentPower;
  syncDebugEconomy(player, '<');

	for (auto& psStruct : powerStructList(player))
	{
		if (psStruct->getStats()->type == STRUCTURE_TYPE::POWER_GEN &&
        psStruct->getState() == STRUCTURE_STATE::BUILT) {
			updateCurrentPower(psStruct.get(), player, ticks);
		}
	}
	syncDebug("updatePlayerPower%u %" PRId64"->%" PRId64"", player, powerBefore, asPower[player].currentPower);
	asPower[player].powerGeneratedLastUpdate = asPower[player].currentPower - powerBefore;
	syncDebugEconomy(player, '<');
}

/* Updates the current power based on the extracted power and a Power Generator*/
static void updateCurrentPower(Structure* psStruct, unsigned player, int ticks)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Invalid player %u", player);
	auto psPowerGen = dynamic_cast<PowerGenerator*>(psStruct);
	ASSERT_OR_RETURN(, psPowerGen != nullptr, "Null pFunctionality?");

	//each power gen can cope with its associated resource extractors
	int64_t extractedPower = 0;
	for (auto i = 0; i < NUM_POWER_MODULES; ++i)
	{
		auto extractor = psPowerGen->getExtractor(i);
		if (extractor && extractor->damageManager->isDead()) {
			syncDebugStructure(extractor, '-');
			extractor = nullptr; // Clear pointer.
		}
		if (extractor) {
			extractedPower += updateExtractedPower(extractor);
		}
	}

	auto multiplier = getBuildingPowerPoints(psStruct);
	syncDebug("updateCurrentPower%d = %" PRId64",%u", player, extractedPower, multiplier);

	asPower[player].currentPower += (extractedPower * multiplier) / 100 * ticks;
	asPower[player].extractedPower += (extractedPower * multiplier) / 100 * ticks;
	ASSERT(asPower[player].currentPower >= 0, "negative power");
	if (asPower[player].currentPower > asPower[player].maxStorage) {
		asPower[player].wastedPower += asPower[player].currentPower - asPower[player].maxStorage;
		asPower[player].currentPower = asPower[player].maxStorage;
	}
}

void setPower(unsigned player, int64_t power)
{
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Invalid player (%u)", player);
	syncDebug("setPower%d %" PRId64"->%d", player, asPower[player].currentPower, power);
	asPower[player].currentPower = power;
	ASSERT(asPower[player].currentPower >= 0, "negative power");
}

int64_t getPower(unsigned player)
{
	ASSERT_OR_RETURN(0, player < MAX_PLAYERS, "Invalid player (%u)", player);
	return asPower[player].currentPower;
}

int64_t getExtractedPower(unsigned player)
{
	ASSERT_OR_RETURN(0, player < MAX_PLAYERS, "Invalid player (%u)", player);
	return asPower[player].extractedPower;
}

int64_t getWastedPower(unsigned player)
{
	ASSERT_OR_RETURN(0, player < MAX_PLAYERS, "Invalid player (%u)", player);
	return asPower[player].wastedPower;
}

int64_t getPowerMinusQueued(unsigned player)
{
	if (player >= MAX_PLAYERS) {
		return 0;
	}
	return (asPower[player].currentPower - getPreciseQueuedPower(player));
}

/// Get the approximate power generated per second for the specified player (for display purposes - not to be used for calculations)
std::string getApproxPowerGeneratedPerSecForDisplay(unsigned player)
{
	if (player >= MAX_PLAYERS) return {};
	auto floatingValue = static_cast<double>(asPower[player].powerGeneratedLastUpdate) *
          static_cast<double>(GAME_UPDATES_PER_SEC);

	return fmt::format("{:+.0f}", floatingValue);
}

bool requestPower(Structure* psStruct, int64_t amount)
{
	if (amount <= 0 || !powerCalculated) return true;

	auto haveEnoughPower = addPowerRequest(psStruct->playerManager->getPlayer(),
                                         psStruct->getId(), amount);
	if (haveEnoughPower) {
		// you can have it
		asPower[psStruct->playerManager->getPlayer()].currentPower -= amount;
		delPowerRequest(psStruct);
		syncDebug("requestPrecisePowerFor%d,%u amount%" PRId64"",
              psStruct->playerManager->getPlayer(), psStruct->getId(), amount);
		return true;
	}
	syncDebug("requestPrecisePowerFor%d,%u wait,amount%" PRId64"",
            psStruct->playerManager->getPlayer(), psStruct->getId(), amount);
	return false; // Not enough power in the queue.
}
