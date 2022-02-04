/*
	This file is part of Warzone 2100.
	Copyright (C) 2020-2021  Warzone 2100 Project

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

#ifndef __INCLUDED_WZSCRIPTDEBUG_H__
#define __INCLUDED_WZSCRIPTDEBUG_H__

#include "lib/framework/frame.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"

#include <functional>
#include <list>
#include <memory>

#include "basedef.h"
#include "wzapi.h"
#include <unordered_map>
#include "qtscript.h"
#include "baseobject.h"

class MultibuttonWidget;
class ScrollableListWidget;
class WzScriptLabelsPanel;

typedef std::unordered_map<wzapi::scripting_instance*, nlohmann::json> MODELMAP;

class WZScriptDebugger : public W_FORM
{
public:
	WZScriptDebugger(std::shared_ptr<scripting_engine::DebugInterface>  debugInterface, bool readOnly);
	~WZScriptDebugger() override;

	static std::shared_ptr<WZScriptDebugger> make(
		const std::shared_ptr<scripting_engine::DebugInterface>& debugInterface, bool readOnly);

	void display(int xOffset, int yOffset) override;

public:
	void selected(BaseObject const* psObj);
	void updateMessages();

	const MODELMAP& getModelMap() const { return modelMap; }
	std::vector<scripting_engine::timerNodeSnapshot> getTriggerSnapshot() const { return trigger_snapshot; }
	const std::vector<scripting_engine::LabelInfo>& getLabelModel() const { return labels; }

public:
	void updateModelMap();
	void updateTriggerSnapshot();
	void updateLabelModel();

protected:
	friend class WzScriptLabelsPanel;
	void labelClear();
	void labelClickedAll();
	void labelClickedActive();
	void labelClicked(const std::string& label);

private:
	std::shared_ptr<W_FORM> createMainPanel() const;
	std::shared_ptr<WIDGET> createSelectedPanel();
	std::shared_ptr<WIDGET> createContextsPanel();
	std::shared_ptr<WIDGET> createPlayersPanel();
	std::shared_ptr<WIDGET> createTriggersPanel();
	std::shared_ptr<WIDGET> createMessagesPanel();
	std::shared_ptr<WIDGET> createLabelsPanel();

private:
	enum class ScriptDebuggerPanel
	{
		Main = 0,
		Selected,
		Contexts,
		Players,
		Triggers,
		Messages,
		Labels
	};

	static void addTextTabButton(const std::shared_ptr<MultibuttonWidget>& mbw, ScriptDebuggerPanel value,
	                             const char* text);
	void switchPanel(ScriptDebuggerPanel newPanel);

private:
	struct SelectedObjectId
	{
		OBJECT_TYPE type = OBJECT_TYPE::COUNT;
		unsigned id = -1;
		unsigned player = -1;

		explicit SelectedObjectId(BaseObject const* psObj)
		{
			if (!psObj) { return; }
			type = getObjectType(psObj);
			id = psObj->getId();
			player = psObj->playerManager->getPlayer();
		}
	};

	std::shared_ptr<scripting_engine::DebugInterface> debugInterface;

	WzText cachedTitleText;
	MODELMAP modelMap;
	std::vector<scripting_engine::timerNodeSnapshot> trigger_snapshot;
	std::vector<scripting_engine::LabelInfo> labels;
	nlohmann::ordered_json selectedObjectDetails;
	optional<SelectedObjectId> selectedObjectId;

	std::shared_ptr<MultibuttonWidget> pageTabs = nullptr;

	ScriptDebuggerPanel currentPanel = ScriptDebuggerPanel::Main;
	std::shared_ptr<WIDGET> currentPanelWidget = nullptr;

	bool readOnly = false;

private:
	std::shared_ptr<W_BUTTON> createButton(int row, const std::string& text, const std::function<void ()>& onClickFunc,
	                                       const std::shared_ptr<WIDGET>& parent,
	                                       const std::shared_ptr<W_BUTTON>& previousButton = nullptr);
};

typedef std::function<void ()> jsDebugShutdownHandlerFunction;
void jsDebugCreate(const std::shared_ptr<scripting_engine::DebugInterface>& debugInterface,
                   const jsDebugShutdownHandlerFunction& shutdownFunc, bool readOnly = false);
bool jsDebugShutdown();
void jsDebugUpdateLabels();

// jsDebugSelected() and jsDebugMessageUpdate() defined in qtscript.h since it is used widely

#endif
