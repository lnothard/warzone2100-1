/*
	This file is part of Warzone 2100.
	Copyright (C) 2011-2021  Warzone 2100 Project

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

#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <memory>
#include <functional>

#include "lib/framework/wzconfig.h"

#include "droid.h"

extern bool allowDesign;
extern bool includeRedundantDesigns;
extern bool playerBuiltHQ;

bool initTemplates();

/// Take ownership of template given by pointer.
/// Returns a new usable DroidTemplate *
DroidTemplate* addTemplate(unsigned player, std::unique_ptr<DroidTemplate> psTemplate);

/// Make a duplicate of template given by pointer and store it. Then return pointer to copy.
DroidTemplate* copyTemplate(unsigned player, DroidTemplate* psTemplate);

void enumerateTemplates(unsigned player, const std::function<bool (DroidTemplate* psTemplate)>& func);
DroidTemplate* findPlayerTemplateById(unsigned player, UDWORD templateId);
std::size_t templateCount(unsigned player);

void clearTemplates(unsigned player);
bool shutdownTemplates();
bool storeTemplates();

bool loadDroidTemplates(const char* filename);

/// return whether a template is for an IDF droid
bool templateIsIDF(DroidTemplate* psTemplate);

/// Fills the list with Templates that can be manufactured in the Factory - based on size
std::vector<DroidTemplate*> fillTemplateList(STRUCTURE* psFactory);

/* gets a template from its name - relies on the name being unique */
const DroidTemplate* getTemplateFromTranslatedNameNoPlayer(char const* pName);

/*getTemplateFromMultiPlayerID gets template for unique ID  searching all lists */
DroidTemplate* getTemplateFromMultiPlayerID(UDWORD multiPlayerID);

/// Have we researched the components of this template?
bool researchedTemplate(const DroidTemplate* psCurr, unsigned player, bool allowRedundant = false, bool verbose = false);

void listTemplates();

nlohmann::json saveTemplateCommon(const DroidTemplate* psCurr);
bool loadTemplateCommon(WzConfig& ini, DroidTemplate& outputTemplate);

void checkPlayerBuiltHQ(const Structure* psStruct);

#endif // TEMPLATE_H
