/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2021  Warzone 2100 Project

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
 * @file template.cpp
 * Droid template functions
 */

#include "lib/framework/frame.h"
#include "lib/framework/wzconfig.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/strres.h"
#include "lib/netplay/netplay.h"

#include "template.h"

#include <memory>
#include "mission.h"
#include "objects.h"
#include "droid.h"
#include "design.h"
#include "hci.h"
#include "multiplay.h"
#include "projectile.h"
#include "main.h"

// Template storage
std::map<unsigned, std::unique_ptr<DroidTemplate>> droidTemplates[MAX_PLAYERS];
std::vector<std::unique_ptr<DroidTemplate>> replacedDroidTemplates[MAX_PLAYERS];

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

bool allowDesign = true;
bool includeRedundantDesigns = false;
bool playerBuiltHQ = false;

static bool researchedItem(const DroidTemplate* /*psCurr*/, unsigned player, COMPONENT_TYPE partIndex, int part,
                           bool allowZero, bool allowRedundant)
{
	ASSERT_PLAYER_OR_RETURN(false, player);
	if (allowZero && part <= 0)
	{
		return true;
	}
	int availability = apCompLists[player][partIndex][part];
	return availability == AVAILABLE || (allowRedundant && availability == REDUNDANT);
}

static bool researchedPart(const DroidTemplate* psCurr, unsigned player, COMPONENT_TYPE partIndex, bool allowZero,
                           bool allowRedundant)
{
	return researchedItem(psCurr, player, partIndex, psCurr->asParts[partIndex], allowZero, allowRedundant);
}

static bool researchedWeap(const DroidTemplate* psCurr, unsigned player, int weapIndex, bool allowRedundant)
{
	ASSERT_PLAYER_OR_RETURN(false, player);
	int availability = apCompLists[player][(int)COMPONENT_TYPE::WEAPON][psCurr->weapons[weapIndex]];
	return availability == AVAILABLE || (allowRedundant && availability == REDUNDANT);
}

bool researchedTemplate(const DroidTemplate* psCurr, unsigned player, bool allowRedundant, bool verbose)
{
	ASSERT_OR_RETURN(false, psCurr, "Given a null template");
	ASSERT_PLAYER_OR_RETURN(false, player);
	bool resBody = researchedPart(psCurr, player, COMPONENT_TYPE::BODY, false, allowRedundant);
	bool resBrain = researchedPart(psCurr, player, COMPONENT_TYPE::BRAIN, true, allowRedundant);
	bool resProp = researchedPart(psCurr, player, COMPONENT_TYPE::PROPULSION, false, allowRedundant);
	bool resSensor = researchedPart(psCurr, player, COMPONENT_TYPE::SENSOR, true, allowRedundant);
	bool resEcm = researchedPart(psCurr, player, COMPONENT_TYPE::ECM, true, allowRedundant);
	bool resRepair = researchedPart(psCurr, player, COMPONENT_TYPE::REPAIR_UNIT, true, allowRedundant);
	bool resConstruct = researchedPart(psCurr, player, COMPONENT_TYPE::CONSTRUCT, true, allowRedundant);
	bool researchedEverything = resBody && resBrain && resProp && resSensor && resEcm && resRepair && resConstruct;
	if (verbose && !researchedEverything)
	{
		debug(LOG_ERROR, "%s : not researched : body=%d brai=%d prop=%d sensor=%d ecm=%d rep=%d con=%d",
		      getStatsName(psCurr),
		      (int)resBody, (int)resBrain, (int)resProp, (int)resSensor, (int)resEcm, (int)resRepair,
		      (int)resConstruct);
	}
	for (int weapIndex = 0; weapIndex < psCurr->weapons.size() && researchedEverything; ++weapIndex)
	{
		researchedEverything = researchedWeap(psCurr, player, weapIndex, allowRedundant);
		if (!researchedEverything && verbose)
		{
			debug(LOG_ERROR, "%s : not researched weapon %u", getStatsName(psCurr), weapIndex);
		}
	}
	return researchedEverything;
}

bool droidTemplate_LoadPartByName(COMPONENT_TYPE compType, const WzString& name, DroidTemplate& outputTemplate)
{
	int index = getCompFromName(compType, name);
	if (index < 0)
	{
		debug(LOG_ERROR, "Stored template contains an unknown (type: %d) component: %s.", compType,
		      name.toUtf8().c_str());
		return false;
	}
	if (index > UINT8_MAX)
	{
		// returned index exceeds uint8_t max - consider changing type of asParts in DROID_TEMPLATE?
		debug(LOG_ERROR, "Stored template contains a (type: %d) component (%s) index that exceeds UINT8_MAX: %d",
		      compType, name.toUtf8().c_str(), index);
		return false;
	}
	outputTemplate.asParts[compType] = static_cast<uint8_t>(index);
	return true;
}

bool droidTemplate_LoadWeapByName(size_t destIndex, const WzString& name, DroidTemplate& outputTemplate)
{
	int index = getCompFromName(COMPONENT_TYPE::WEAPON, name);
	if (index < 0)
	{
		debug(LOG_ERROR, "Stored template contains an unknown (type: %d) component: %s.", COMPONENT_TYPE::WEAPON,
		      name.toUtf8().c_str());
		return false;
	}
#if INT_MAX > UINT32_MAX
	if (index > (int)UINT32_MAX)
	{
		// returned index exceeds uint32_t max - consider changing type of asWeaps in DROID_TEMPLATE?
		debug(LOG_ERROR, "Stored template contains a (type: %d) component (%s) index that exceeds UINT32_MAX: %d", COMPONENT_TYPE::WEAPON, name.toUtf8().c_str(), index);
		return false;
	}
#endif
	outputTemplate.asWeaps[destIndex] = static_cast<uint32_t>(index);
	return true;
}

bool loadTemplateCommon(WzConfig& ini, DroidTemplate& outputTemplate)
{
	DroidTemplate& design = outputTemplate;
	design.name = ini.string("name");
	WzString droidType = ini.value("type").toWzString();

	if (droidType == "ECM")
	{
		design.type = DROID_TYPE::ECM;
	}
	else if (droidType == "SENSOR")
	{
		design.type = DROID_TYPE::SENSOR;
	}
	else if (droidType == "CONSTRUCT")
	{
		design.type = DROID_TYPE::CONSTRUCT;
	}
	else if (droidType == "WEAPON")
	{
		design.type = DROID_TYPE::WEAPON;
	}
	else if (droidType == "PERSON")
	{
		design.type = DROID_TYPE::PERSON;
	}
	else if (droidType == "CYBORG")
	{
		design.type = DROID_TYPE::CYBORG;
	}
	else if (droidType == "CYBORG_SUPER")
	{
		design.type = DROID_TYPE::CYBORG_SUPER;
	}
	else if (droidType == "CYBORG_CONSTRUCT")
	{
		design.type = DROID_TYPE::CYBORG_CONSTRUCT;
	}
	else if (droidType == "CYBORG_REPAIR")
	{
		design.type = DROID_TYPE::CYBORG_REPAIR;
	}
	else if (droidType == "TRANSPORTER")
	{
		design.type = DROID_TYPE::TRANSPORTER;
	}
	else if (droidType == "SUPERTRANSPORTER")
	{
		design.type = DROID_TYPE::SUPER_TRANSPORTER;
	}
	else if (droidType == "DROID")
	{
		design.type = DROID_TYPE::DEFAULT;
	}
	else if (droidType == "DROID_COMMAND")
	{
		design.type = DROID_TYPE::COMMAND;
	}
	else if (droidType == "REPAIR")
	{
		design.type = DROID_TYPE::REPAIRER;
	}
	else
	{
		ASSERT(false, "No such droid type \"%s\" for %s", droidType.toUtf8().c_str(), getID(&design));
	}

	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::BODY, ini.value("body").toWzString(), design)) return false;
	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::BRAIN, ini.value("brain", WzString("ZNULLBRAIN")).toWzString(), design))
		return false;
	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::PROPULSION, ini.value("propulsion", WzString("ZNULLPROP")).toWzString(),
	                                  design)) return false;
	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::REPAIR_UNIT, ini.value("repair", WzString("ZNULLREPAIR")).toWzString(),
	                                  design)) return false;
	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::ECM, ini.value("ecm", WzString("ZNULLECM")).toWzString(), design)) return
		false;
	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::SENSOR, ini.value("sensor", WzString("ZNULLSENSOR")).toWzString(), design))
		return false;
	if (!droidTemplate_LoadPartByName(COMPONENT_TYPE::CONSTRUCT, ini.value("construct", WzString("ZNULLCONSTRUCT")).toWzString(),
	                                  design)) return false;

	std::vector<WzString> weapons = ini.value("weapons").toWzStringList();
	ASSERT(weapons.size() <= MAX_WEAPONS, "Number of weapons (%zu) exceeds MAX_WEAPONS (%d)", weapons.size(),
	       MAX_WEAPONS);
	design.weaponCount = (weapons.size() <= MAX_WEAPONS) ? (int8_t)weapons.size() : MAX_WEAPONS;
	if (!droidTemplate_LoadWeapByName(0, (!weapons.empty()) ? weapons[0] : "ZNULLWEAPON", design)) return false;
	if (!droidTemplate_LoadWeapByName(1, (weapons.size() >= 2) ? weapons[1] : "ZNULLWEAPON", design)) return false;
	if (!droidTemplate_LoadWeapByName(2, (weapons.size() >= 3) ? weapons[2] : "ZNULLWEAPON", design)) return false;

	return true;
}

bool initTemplates()
{
	if (selectedPlayer >= MAX_PLAYERS) { return false; }

	WzConfig ini("userdata/" + WzString(rulesettag) + "/templates.json", WzConfig::ReadOnly);
	if (!ini.status())
	{
		debug(LOG_WZ, "Could not open %s", ini.fileName().toUtf8().c_str());
		return false;
	}
	int version = ini.value("version", 0).toInt();
	if (version == 0)
	{
		return true; // too old version
	}
	for (ini.beginArray("templates"); ini.remainingArrayItems(); ini.nextArrayItem())
	{
		DroidTemplate design;
		bool loadCommonSuccess = loadTemplateCommon(ini, design);
		design.id = generateNewObjectId();
		design.isPrefab = false; // not AI template
		design.isStored = true;

		if (!loadCommonSuccess)
		{
			debug(LOG_ERROR, "Stored template \"%s\" contains an unknown component.", design.name.toUtf8().c_str());
			continue;
		}

		char const* failPart = nullptr;
		WzString failPartName;
		auto designablePart = [&](ComponentStats const& component, char const* part)
		{
			if (!component.designable)
			{
				failPart = part;
				failPartName = component.name;
			}
			return component.designable;
		};

		bool designable =
			designablePart(asBodyStats[design.asParts[COMPONENT_TYPE::BODY]], "Body")
			&& designablePart(asPropulsionStats[design.asParts[COMPONENT_TYPE::PROPULSION]], "Propulsion")
			&& (design.asParts[COMPONENT_TYPE::BRAIN] == 0 || designablePart(asBrainStats[design.asParts[COMPONENT_TYPE::BRAIN]], "Brain"))
			&& (design.asParts[COMPONENT_TYPE::REPAIR_UNIT] == 0 || designablePart(asRepairStats[design.asParts[COMPONENT_TYPE::REPAIR_UNIT]],
			                                                           "Repair unit"))
			&& (design.asParts[COMPONENT_TYPE::ECM] == 0 || designablePart(asECMStats[design.asParts[COMPONENT_TYPE::ECM]], "ECM"))
			&& (design.asParts[COMPONENT_TYPE::SENSOR] == 0 ||
				designablePart(asSensorStats[design.asParts[COMPONENT_TYPE::SENSOR]], "Sensor"))
			&& (design.asParts[COMPONENT_TYPE::CONSTRUCT] == 0 || designablePart(asConstructStats[design.asParts[COMPONENT_TYPE::CONSTRUCT]],
			                                                          "Construction part"))
			&& (design.weaponCount <= 0 || asBrainStats[design.asParts[COMPONENT_TYPE::BRAIN]].psWeaponStat == &asWeaponStats[design.
					asWeaps[0]]
          || designablePart(asWeaponStats[design.asWeaps[0]], "Weapon 0"))
			&& (design.weaponCount <= 1 || designablePart(asWeaponStats[design.asWeaps[1]], "Weapon 1"))
			&& (design.weaponCount <= 2 || designablePart(asWeaponStats[design.asWeaps[2]], "Weapon 2"));
		if (!designable)
		{
			debug(LOG_ERROR, "%s \"%s\" for \"%s\" from stored templates cannot be designed", failPart,
			      failPartName.toUtf8().c_str(), design.name.toUtf8().c_str());
			continue;
		}
		bool valid = intValidTemplate(&design, ini.value("name").toWzString().toUtf8().c_str(), false, selectedPlayer);
		if (!valid)
		{
			debug(LOG_ERROR, "Invalid template \"%s\" from stored templates", design.name.toUtf8().c_str());
			continue;
		}
		DroidTemplate* psDestTemplate = nullptr;
		for (auto& keyvaluepair : droidTemplates[selectedPlayer])
		{
			psDestTemplate = keyvaluepair.second.get();
			// Check if template is identical to a loaded template
			if (psDestTemplate->type == design.type
				&& psDestTemplate->name.compare(design.name) == 0
				&& psDestTemplate->weaponCount == design.weaponCount
				&& psDestTemplate->asWeaps[0] == design.asWeaps[0]
				&& psDestTemplate->asWeaps[1] == design.asWeaps[1]
				&& psDestTemplate->asWeaps[2] == design.asWeaps[2]
				&& psDestTemplate->asParts[COMPONENT_TYPE::BODY] == design.asParts[COMPONENT_TYPE::BODY]
				&& psDestTemplate->asParts[COMPONENT_TYPE::PROPULSION] == design.asParts[COMPONENT_TYPE::PROPULSION]
				&& psDestTemplate->asParts[COMPONENT_TYPE::REPAIR_UNIT] == design.asParts[COMPONENT_TYPE::REPAIR_UNIT]
				&& psDestTemplate->asParts[COMPONENT_TYPE::ECM] == design.asParts[COMPONENT_TYPE::ECM]
				&& psDestTemplate->asParts[COMPONENT_TYPE::SENSOR] == design.asParts[COMPONENT_TYPE::SENSOR]
				&& psDestTemplate->asParts[COMPONENT_TYPE::CONSTRUCT] == design.asParts[COMPONENT_TYPE::CONSTRUCT]
				&& psDestTemplate->asParts[COMPONENT_TYPE::BRAIN] == design.asParts[COMPONENT_TYPE::BRAIN])
			{
				break;
			}
			psDestTemplate = nullptr;
		}
		if (psDestTemplate)
		{
			psDestTemplate->isStored = true; // assimilate it
			continue; // next!
		}
		design.isEnabled = allowDesign;
		copyTemplate(selectedPlayer, &design);
		localTemplates.push_back(design);
	}
	ini.endArray();
	return true;
}

nlohmann::json saveTemplateCommon(const DroidTemplate* psCurr)
{
	nlohmann::json templateObj = nlohmann::json::object();
	templateObj["name"] = psCurr->name;
	switch (psCurr->type)
	{
    case DROID_TYPE::ECM: templateObj["type"] = "ECM";
		break;
    case DROID_TYPE::SENSOR: templateObj["type"] = "SENSOR";
		break;
  case DROID_TYPE::CONSTRUCT: templateObj["type"] = "CONSTRUCT";
		break;
  case DROID_TYPE::WEAPON: templateObj["type"] = "WEAPON";
		break;
  case DROID_TYPE::PERSON: templateObj["type"] = "PERSON";
		break;
  case DROID_TYPE::CYBORG: templateObj["type"] = "CYBORG";
		break;
  case DROID_TYPE::CYBORG_SUPER: templateObj["type"] = "CYBORG_SUPER";
		break;
  case DROID_TYPE::CYBORG_CONSTRUCT: templateObj["type"] = "CYBORG_CONSTRUCT";
		break;
  case DROID_TYPE::CYBORG_REPAIR: templateObj["type"] = "CYBORG_REPAIR";
		break;
  case DROID_TYPE::TRANSPORTER: templateObj["type"] = "TRANSPORTER";
		break;
  case DROID_TYPE::SUPER_TRANSPORTER: templateObj["type"] = "SUPERTRANSPORTER";
		break;
  case DROID_TYPE::COMMAND: templateObj["type"] = "DROID_COMMAND";
		break;
  case DROID_TYPE::REPAIRER: templateObj["type"] = "REPAIR";
		break;
  case DROID_TYPE::DEFAULT: templateObj["type"] = "DROID";
		break;
	default: ASSERT(false, "No such droid type \"%d\" for %s", psCurr->type, psCurr->name.toUtf8().c_str());
	}
	templateObj["body"] = (asBodyStats + psCurr->asParts[COMPONENT_TYPE::BODY])->id;
	templateObj["propulsion"] = (asPropulsionStats + psCurr->asParts[COMPONENT_TYPE::PROPULSION])->id;
	if (psCurr->asParts[COMPONENT_TYPE::BRAIN] != 0)
	{
		templateObj["brain"] = (asBrainStats + psCurr->asParts[COMPONENT_TYPE::BRAIN])->id;
	}
	if ((asRepairStats + psCurr->asParts[COMPONENT_TYPE::REPAIR_UNIT])->location == LOC::TURRET) // avoid auto-repair...
	{
		templateObj["repair"] = (asRepairStats + psCurr->asParts[COMPONENT_TYPE::REPAIR_UNIT])->id;
	}
	if ((asECMStats + psCurr->asParts[COMPONENT_TYPE::ECM])->location == LOC::TURRET)
	{
		templateObj["ecm"] = (asECMStats + psCurr->asParts[COMPONENT_TYPE::ECM])->id;
	}
	if ((asSensorStats + psCurr->asParts[COMPONENT_TYPE::SENSOR])->location == LOC::TURRET)
	{
		templateObj["sensor"] = (asSensorStats + psCurr->asParts[COMPONENT_TYPE::SENSOR])->id;
	}
	if (psCurr->asParts[COMPONENT_TYPE::CONSTRUCT] != 0)
	{
		templateObj["construct"] = (asConstructStats + psCurr->asParts[COMPONENT_TYPE::CONSTRUCT])->id;
	}
	nlohmann::json weapons = nlohmann::json::array();
	for (int j = 0; j < psCurr->weaponCount; j++)
	{
		weapons.push_back((asWeaponStats + psCurr->asWeaps[j])->id);
	}
	if (!weapons.empty())
	{
		templateObj["weapons"] = std::move(weapons);
	}
	return templateObj;
}

bool storeTemplates()
{
	if (selectedPlayer >= MAX_PLAYERS) { return false; }

	// Write stored templates (back) to file
	WzConfig ini("userdata/" + WzString(rulesettag) + "/templates.json", WzConfig::ReadAndWrite);
	if (!ini.status() || !ini.isWritable())
	{
		debug(LOG_ERROR, "Could not open %s", ini.fileName().toUtf8().c_str());
		return false;
	}
	ini.setValue("version", 1); // for breaking backwards compatibility in a nice way
	ini.beginArray("templates");
	for (auto& keyvaluepair : droidTemplates[selectedPlayer])
	{
		const DroidTemplate* psCurr = keyvaluepair.second.get();
		if (psCurr->isStored)
		{
			ini.currentJsonValue() = saveTemplateCommon(psCurr);
			ini.nextArrayItem();
		}
	}
	ini.endArray();
	return true;
}

bool shutdownTemplates()
{
	return storeTemplates();
}

DroidTemplate::DroidTemplate()
// This constructor replaces a memset in scrAssembleWeaponTemplate(), not needed elsewhere.
	: BaseStats(STAT_TEMPLATE)
	  //, asParts
	  , weaponCount(0)
	  //, asWeaps
	  , type(DROID_WEAPON)
	  , id(0)
	  , isPrefab(false)
	  , isStored(false)
	  , isEnabled(false)
{
	std::fill_n(asParts, DROID_MAXCOMP, static_cast<uint8_t>(0));
	std::fill_n(asWeaps, MAX_WEAPONS, 0);
}

bool loadDroidTemplates(const char* filename)
{
	WzConfig ini(filename, WzConfig::ReadOnlyAndRequired);
	std::vector<WzString> list = ini.childGroups();
	for (auto & i : list)
	{
		ini.beginGroup(i);
		DroidTemplate design;
		if (!loadTemplateCommon(ini, design))
		{
			debug(LOG_ERROR, "Stored template \"%s\" contains an unknown component.",
			      ini.string("name").toUtf8().c_str());
			continue;
		}
		design.id = i;
		design.name = ini.string("name");
		design.id = generateNewObjectId();
		design.isPrefab = true;
		design.isStored = false;
		design.isEnabled = true;
		bool available = ini.value("available", false).toBool();
		char const* droidResourceName = getDroidResourceName(i.toUtf8().c_str());
		design.name = WzString::fromUtf8(droidResourceName != nullptr
			                                 ? droidResourceName
			                                 : GetDefaultTemplateName(&design));
		ini.endGroup();

		for (unsigned playerIdx = 0; playerIdx < MAX_PLAYERS; ++playerIdx)
		{
			// Give those meant for humans to all human players.
			if (NetPlay.players[playerIdx].allocated && available)
			{
				design.isPrefab = false;
				copyTemplate(playerIdx, &design);

				// This sets up the UI templates for display purposes ONLY--we still only use droidTemplates for making them.
				// FIXME: Why are we doing this here, and not on demand ?
				// Only add unique designs to the UI list (Note, perhaps better to use std::map instead?)
				std::list<DroidTemplate>::iterator it;
				for (it = localTemplates.begin(); it != localTemplates.end(); ++it)
				{
					DroidTemplate* psCurr = &*it;
					if (psCurr->id == design.id)
					{
						debug(LOG_WARNING, "Design id:%d (%s) *NOT* added to UI list (duplicate), player= %d",
                  design.id, getStatsName(&design), playerIdx);
						break;
					}
				}
				if (it == localTemplates.end())
				{
					debug(LOG_NEVER, "Design id:%d (%s) added to UI list, player =%d", design.id,
					      getStatsName(&design), playerIdx);
					localTemplates.push_back(design);
				}
			}
			else if (!NetPlay.players[playerIdx].allocated) // AI template
			{
				design.isPrefab = true; // prefabricated templates referenced from VLOs
				copyTemplate(playerIdx, &design);
			}
		}
		debug(LOG_NEVER, "Droid template found, Name: %s, MP ID: %d, ref: %u, ID: %s, prefab: %s, type:%d (loading)",
          getStatsName(&design), design.id, design.ref, getID(&design), design.isPrefab ? "yes" : "no",
          design.type);
	}

	return true;
}

DroidTemplate* copyTemplate(unsigned player, DroidTemplate* psTemplate)
{
	auto dup = std::make_unique<DroidTemplate>(*psTemplate);
	return addTemplate(player, std::move(dup));
}

DroidTemplate* addTemplate(unsigned player, std::unique_ptr<DroidTemplate> psTemplate)
{
	ASSERT_PLAYER_OR_RETURN(nullptr, player);
	UDWORD multiPlayerID = psTemplate->id;
	auto it = droidTemplates[player].find(multiPlayerID);
	if (it != droidTemplates[player].end())
	{
		// replacing existing template
		it->second.swap(psTemplate);
		replacedDroidTemplates[player].push_back(std::move(psTemplate));
		return it->second.get();
	}
	else
	{
		// new template
		auto result = droidTemplates[player].insert(
			std::pair<UDWORD, std::unique_ptr<DroidTemplate>>(multiPlayerID, std::move(psTemplate)));
		return result.first->second.get();
	}
}

void enumerateTemplates(unsigned player, const std::function<bool (DroidTemplate* psTemplate)>& func)
{
	ASSERT_PLAYER_OR_RETURN(, player);
	for (auto& keyvaluepair : droidTemplates[player])
	{
		if (!func(keyvaluepair.second.get()))
		{
			break;
		}
	}
}

DroidTemplate* findPlayerTemplateById(unsigned player, UDWORD templateId)
{
	ASSERT_PLAYER_OR_RETURN(nullptr, player);
	auto it = droidTemplates[player].find(templateId);
	if (it != droidTemplates[player].end())
	{
		return it->second.get();
	}
	return nullptr;
}

size_t templateCount(unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return droidTemplates[player].size();
}

void clearTemplates(unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(, player);
	droidTemplates[player].clear();
	replacedDroidTemplates[player].clear();
}

//free the storage for the droid templates
bool droidTemplateShutDown()
{
	for (unsigned player = 0; player < MAX_PLAYERS; player++)
	{
		clearTemplates(player);
	}
	localTemplates.clear();
	return true;
}

/*!
 * Get a static template from its name. This is used from scripts. These templates must
 * never be changed or deleted.
 * \param pName Template name
 * \pre pName has to be the unique, untranslated name!
 */
const DroidTemplate* getTemplateFromTranslatedNameNoPlayer(char const* pName)
{
	for (auto& droidTemplate : droidTemplates)
	{
		for (auto& keyvaluepair : droidTemplate)
		{
			if (keyvaluepair.second->id.compare(pName) == 0)
			{
				return keyvaluepair.second.get();
			}
		}
	}
	return nullptr;
}

/*getTemplatefFromMultiPlayerID gets template for unique ID  searching all lists */
DroidTemplate* getTemplateFromMultiPlayerID(UDWORD multiPlayerID)
{
	for (auto& droidTemplate : droidTemplates)
	{
		if (droidTemplate.count(multiPlayerID) > 0)
		{
			return droidTemplate[multiPlayerID].get();
		}
	}
	return nullptr;
}

/*called when a Template is deleted in the Design screen*/
void deleteTemplateFromProduction(DroidTemplate* psTemplate, unsigned player, QUEUE_MODE mode)
{
	Structure* psStruct;
	Structure* psList;

	ASSERT_OR_RETURN(, psTemplate != nullptr, "Null psTemplate");
	ASSERT_OR_RETURN(, player < MAX_PLAYERS, "Invalid player: %u", player);

	//see if any factory is currently using the template
	for (unsigned i = 0; i < 2; ++i)
	{
		psList = nullptr;
		switch (i)
		{
		case 0:
			psList = apsStructLists[player];
			break;
		case 1:
			psList = mission.apsStructLists[player];
			break;
		}
		for (psStruct = psList; psStruct != nullptr; psStruct = psStruct->psNext)
		{
			if (StructIsFactory(psStruct))
			{
				if (psStruct->pFunctionality == nullptr)
				{
					continue;
				}
				Factory* psFactory = &psStruct->pFunctionality->factory;

				if (psFactory->psAssemblyPoint && psFactory->psAssemblyPoint->factoryType < NUM_FACTORY_TYPES
					&& psFactory->psAssemblyPoint->factoryInc < asProductionRun[psFactory->psAssemblyPoint->factoryType]
					.size())
				{
					ProductionRun& productionRun = asProductionRun[psFactory->psAssemblyPoint->factoryType][psFactory->
						psAssemblyPoint->factoryInc];
					for (unsigned inc = 0; inc < productionRun.size(); ++inc)
					{
						if (productionRun[inc].psTemplate && productionRun[inc].psTemplate->id == psTemplate
							->id && mode == ModeQueue)
						{
							//just need to erase this production run entry
							productionRun.erase(productionRun.begin() + inc);
							--inc;
						}
					}
				}

				if (psFactory->psSubject == nullptr)
				{
					continue;
				}

				// check not being built in the factory for the template player
				if (psTemplate->id == psFactory->psSubject->id && mode == ModeImmediate)
				{
					syncDebugStructure(psStruct, '<');
					syncDebug("Clearing production");

					// Clear the factory's subject, and returns power.
					cancelProduction(psStruct, ModeImmediate, false);
					// Check to see if anything left to produce. (Also calls cancelProduction again, if nothing left to produce, which is a no-op. But if other things are left to produce, doesn't call cancelProduction, so wouldn't return power without the explicit cancelProduction call above.)
					doNextProduction(psStruct, nullptr, ModeImmediate);

					syncDebugStructure(psStruct, '>');
				}
			}
		}
	}
}

// return whether a template is for an IDF droid
bool templateIsIDF(DroidTemplate* psTemplate)
{
	//add Cyborgs
	if (!(psTemplate->type == DROID_WEAPON || psTemplate->type == DROID_CYBORG || psTemplate->type ==
                                                                                DROID_CYBORG_SUPER))
	{
		return false;
	}

	if (proj_Direct(psTemplate->asWeaps[0] + asWeaponStats))
	{
		return false;
	}

	return true;
}

void listTemplates()
{
	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "selectedPlayer (%" PRIu32 ") >= MAX_PLAYERS", selectedPlayer);
	for (auto& keyvaluepair : droidTemplates[selectedPlayer])
	{
		DroidTemplate* t = keyvaluepair.second.get();
		debug(LOG_INFO, "template %s : %ld : %s : %s : %s", getStatsName(t), (long)t->id,
          t->isEnabled ? "Enabled" : "Disabled", t->isStored ? "Stored" : "Temporal",
          t->isPrefab ? "Prefab" : "Designed");
	}
}

/*
fills the list with Templates that can be manufactured
in the Factory - based on size. There is a limit on how many can be manufactured
at any one time.
*/
std::vector<DroidTemplate*> fillTemplateList(Structure* psFactory)
{
	std::vector<DroidTemplate*> pList;
	const unsigned player = psFactory->player;

	auto iCapacity = (BODY_SIZE)psFactory->capacity;

	/* Add the templates to the list*/
	for (DroidTemplate& i : localTemplates)
	{
		DroidTemplate* psCurr = &i;
		// Must add droids if currently in production.
		if (!getProduction(psFactory, psCurr).quantity)
		{
			//can only have (MAX_CMDDROIDS) in the world at any one time
			if (psCurr->type == DROID_COMMAND)
			{
				if (checkProductionForCommand(player) + checkCommandExist(player) >= (MAX_CMDDROIDS))
				{
					continue;
				}
			}

			if (!psCurr->isEnabled
				|| (bMultiPlayer && !playerBuiltHQ && (psCurr->type != DROID_CONSTRUCT && psCurr->type !=
                                                                                  DROID_CYBORG_CONSTRUCT))
				|| !validTemplateForFactory(psCurr, psFactory, false)
				|| !researchedTemplate(psCurr, player, includeRedundantDesigns))
			{
				continue;
			}
		}

		//check the factory can cope with this sized body
		if (((asBodyStats + psCurr->asParts[COMPONENT_TYPE::BODY])->size <= iCapacity))
		{
			pList.push_back(psCurr);
		}
		else if (bMultiPlayer && (iCapacity == SIZE_HEAVY))
		{
			// Special case for Super heavy bodyies (Super Transporter)
			if ((asBodyStats + psCurr->asParts[COMPONENT_TYPE::BODY])->size == SIZE_SUPER_HEAVY)
			{
				pList.push_back(psCurr);
			}
		}
	}

	return pList;
}

void checkPlayerBuiltHQ(const Structure* psStruct)
{
	if (selectedPlayer == psStruct->player && psStruct->pStructureType->type == REF_HQ)
	{
		playerBuiltHQ = true;
	}
}
