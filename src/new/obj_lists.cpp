//
// Created by Luna Nothard on 19/12/2021.
//

#include "droid.h"
#include "obj_lists.h"

void create_droid(unsigned id, unsigned player)
{
	droid_lists[player].emplace_back(id, player);
}

void create_structure(unsigned id, unsigned player)
{
	auto new_structure = std::make_unique<Impl::Structure>(id, player);
	structure_lists[player].push_back(new_structure);
}

void destroy_droid(Droid& droid)
{
	std::erase(droid_lists[droid.getPlayer()], droid);
}

void destroy_structure(Structure& structure)
{
	std::erase(structure_lists[structure.getPlayer()], structure);
}
