//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_FACTION_H
#define WARZONE2100_FACTION_H

#include <string>

enum class FACTION_ID
{
    NORMAL,
    NEXUS,
    COLLECTIVE
};

struct Faction
{
    std::string name;
};

#endif //WARZONE2100_FACTION_H
