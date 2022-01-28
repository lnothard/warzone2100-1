/*
	This file is part of Warzone 2100.
	Copyright (C) 2011-2020  Warzone 2100 Project

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

#ifndef __INCLUDED_WZAPI_H__
#define __INCLUDED_WZAPI_H__

// Documentation stuff follows. The build system will parse the comment prefixes
// and sort the comments into the appropriate Markdown documentation files.

//== # Globals
//==
//== This section describes global variables (or 'globals' for short) that are
//== available from all scripts. You typically cannot write to these variables,
//== they are read-only.
//==
//__ # Events
//__
//__ This section describes event callbacks (or 'events' for short) that are
//__ called from the game when something specific happens. Which scripts
//__ receive them is usually filtered by player. Call ```receiveAllEvents(true)```
//__ to start receiving all events unfiltered.
//__
//-- # Functions
//--
//-- This section describes functions that can be called from scripts to make
//-- things happen in the game (usually called our script 'API').
//--
//;; # Game objects
//;;
//;; This section describes various **game objects** defined by the script interface,
//;; and which are both accepted by functions and returned by them. Changing the
//;; fields of a **game object** has no effect on the game before it is passed to a
//;; function that does something with the **game object**.
//;;

#include "lib/framework/frame.h"
#include "lib/framework/wzconfig.h"
#include <optional-lite/optional.hpp>
using nonstd::optional;
using nonstd::nullopt;

#include "basedef.h"
#include "gateway.h"
#include "hci.h"
#include "research.h"


#include <string>
#include <vector>
#include <memory>
#include <functional>

typedef uint64_t uniqueTimerID;

class timerAdditionalData
{
public:
	virtual ~timerAdditionalData() = default;

public:
	virtual void onTimerDelete(uniqueTimerID, BaseObject *)
	{
	};
};

typedef std::function<void (uniqueTimerID, BaseObject *, timerAdditionalData*)> TimerFunc;

// NOTES:
// - All position value types (scr_position, scr_area, etc) passed to/from scripts expect map coordinates

enum timerType
{
	TIMER_REPEAT,
	TIMER_ONESHOT_READY,
	TIMER_ONESHOT_DONE,
	TIMER_REMOVED
};

struct scr_radius
{
	int x;
	int y;
	int radius;
};

struct scr_area
{
	int x1;
	int y1;
	int x2;
	int y2;
};

struct scr_position
{
	int x;
	int y;
};

// Utility conversion functions
BaseObject * IdToObject(unsigned id, unsigned player);

// MARK: - wzapi

namespace wzapi
{
#define WZAPI_NO_PARAMS_NO_CONTEXT
#define WZAPI_NO_PARAMS const wzapi::execution_context& context
#define WZAPI_PARAMS(...) const wzapi::execution_context& context, __VA_ARGS__
#define WZAPI_BASE_PARAMS(...) const wzapi::execution_context_base& context, __VA_ARGS__

#define SCRIPTING_EVENT_NON_REQUIRED { return false; }

	struct researchResult; // forward-declare

	template <typename T>
	struct event_nullable_ptr
	{
	private:
		using TYPE_POINTER = T*;
		TYPE_POINTER pt;

	public:
		explicit event_nullable_ptr(TYPE_POINTER _pt)
			: pt(_pt)
		{
		}

		event_nullable_ptr()
			: pt(nullptr)
		{
		}

		explicit operator TYPE_POINTER&()
		{
			return pt;
		}

		explicit operator TYPE_POINTER() const
		{
			return pt;
		}

		explicit operator bool() const noexcept
		{
			return pt != nullptr;
		}
	};

	class scripting_event_handling_interface
	{
	public:
		virtual ~scripting_event_handling_interface() = default;
	public:
		// MARK: General events

		//__ ## eventGameInit()
		//__
		//__ An event that is run once as the game is initialized. Not all game state may have been
		//__ properly initialized by this time, so use this only to initialize script state.
		//__
		virtual bool handle_eventGameInit() = 0;

		//__ ## eventStartLevel()
		//__
		//__ An event that is run once the game has started and all game data has been loaded.
		//__
		virtual bool handle_eventStartLevel() = 0;

		//__ ## eventMissionTimeout()
		//__
		//__ An event that is run when the mission timer has run out.
		//__
		virtual bool handle_eventMissionTimeout() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventVideoDone()
		//__
		//__ An event that is run when a video show stopped playing.
		//__
		virtual bool handle_eventVideoDone() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventGameLoaded()
		//__
		//__ An event that is run when game is loaded from a saved game. There is usually no need to use this event.
		//__
		virtual bool handle_eventGameLoaded() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventGameSaving()
		//__
		//__ An event that is run before game is saved. There is usually no need to use this event.
		//__
		virtual bool handle_eventGameSaving() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventGameSaved()
		//__
		//__ An event that is run after game is saved. There is usually no need to use this event.
		//__
		virtual bool handle_eventGameSaved() SCRIPTING_EVENT_NON_REQUIRED

	public:
		// MARK: Transporter events

		//__
		//__ ## eventTransporterLaunch(transport)
		//__
		//__ An event that is run when the mission transporter has been ordered to fly off.
		//__
		virtual bool handle_eventLaunchTransporter() SCRIPTING_EVENT_NON_REQUIRED // DEPRECATED!
		virtual bool handle_eventTransporterLaunch(const BaseObject * psTransport) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventTransporterArrived(transport)
		//__
		//__ An event that is run when the mission transporter has arrived at the map edge with reinforcements.
		//__
		virtual bool handle_eventReinforcementsArrived() SCRIPTING_EVENT_NON_REQUIRED // DEPRECATED!
		virtual bool handle_eventTransporterArrived(const BaseObject * psTransport) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventTransporterExit(transport)
		//__
		//__ An event that is run when the mission transporter has left the map.
		//__
		virtual bool handle_eventTransporterExit(const BaseObject * psObj) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventTransporterDone(transport)
		//__
		//__ An event that is run when the mission transporter has no more reinforcements to deliver.
		//__
		virtual bool handle_eventTransporterDone(const BaseObject * psTransport) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventTransporterLanded(transport)
		//__
		//__ An event that is run when the mission transporter has landed with reinforcements.
		//__
		virtual bool handle_eventTransporterLanded(const BaseObject * psTransport) SCRIPTING_EVENT_NON_REQUIRED

	public:
		// MARK: UI-related events (intended for the tutorial)

		//__ ## eventDeliveryPointMoving()
		//__
		//__ An event that is run when the current player starts to move a delivery point.
		//__
		virtual bool handle_eventDeliveryPointMoving(const BaseObject * psStruct) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDeliveryPointMoved()
		//__
		//__ An event that is run after the current player has moved a delivery point.
		//__
		virtual bool handle_eventDeliveryPointMoved(const BaseObject * psStruct) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDesignBody()
		//__
		//__An event that is run when current user picks a body in the design menu.
		//__
		virtual bool handle_eventDesignBody() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDesignPropulsion()
		//__
		//__An event that is run when current user picks a propulsion in the design menu.
		//__
		virtual bool handle_eventDesignPropulsion() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDesignWeapon()
		//__
		//__An event that is run when current user picks a weapon in the design menu.
		//__
		virtual bool handle_eventDesignWeapon() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDesignCommand()
		//__
		//__An event that is run when current user picks a command turret in the design menu.
		//__
		virtual bool handle_eventDesignCommand() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDesignSystem()
		//__
		//__An event that is run when current user picks a system other than command turret in the design menu.
		//__
		virtual bool handle_eventDesignSystem() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventDesignQuit()
		//__
		//__An event that is run when current user leaves the design menu.
		//__
		virtual bool handle_eventDesignQuit() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventMenuBuildSelected()
		//__
		//__An event that is run when current user picks something new in the build menu.
		//__
		virtual bool handle_eventMenuBuildSelected(/*SimpleObject *psObj*/) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventMenuResearchSelected()
		//__
		//__An event that is run when current user picks something new in the research menu.
		//__
		virtual bool handle_eventMenuResearchSelected(/*SimpleObject *psObj*/) SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventMenuBuild()
		//__
		//__An event that is run when current user opens the build menu.
		//__
		virtual bool handle_eventMenuBuild() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventMenuResearch()
		//__
		//__An event that is run when current user opens the research menu.
		//__
		virtual bool handle_eventMenuResearch() SCRIPTING_EVENT_NON_REQUIRED


		virtual bool handle_eventMenuDesign() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventMenuManufacture()
		//__An event that is run when current user opens the manufacture menu.
		//__
		virtual bool handle_eventMenuManufacture() SCRIPTING_EVENT_NON_REQUIRED

		//__ ## eventSelectionChanged(objects)
		//__
		//__ An event that is triggered whenever the host player selects one or more game objects.
		//__ The ```objects``` parameter contains an array of the currently selected game objects.
		//__ Keep in mind that the player may drag and drop select many units at once, select one
		//__ unit specifically, or even add more selections to a current selection one at a time.
		//__ This event will trigger once for each user action, not once for each selected or
		//__ deselected object. If all selected game objects are deselected, ```objects``` will
		//__ be empty.
		//__
		virtual bool handle_eventSelectionChanged(const std::vector<const BaseObject *>& objects)
		SCRIPTING_EVENT_NON_REQUIRED

	public:
		// MARK: Game state-change events

		//__ ## eventObjectRecycled()
		//__
		//__ An event that is run when an object (ex. droid, structure) is recycled.
		//__
		virtual bool handle_eventObjectRecycled(const BaseObject * psObj) = 0;

		//__ ## eventPlayerLeft(player)
		//__
		//__ An event that is run after a player has left the game.
		//__
		virtual bool handle_eventPlayerLeft(unsigned player) = 0;

		//__ ## eventCheatMode(entered)
		//__
		//__ Game entered or left cheat/debug mode.
		//__ The entered parameter is true if cheat mode entered, false otherwise.
		//__
		virtual bool handle_eventCheatMode(bool entered) = 0;

		//__ ## eventDroidIdle(droid)
		//__
		//__ A droid should be given new orders.
		//__
		virtual bool handle_eventDroidIdle(const Droid* psDroid) = 0;

		//__ ## eventDroidBuilt(droid[, structure])
		//__
		//__ An event that is run every time a droid is built. The structure parameter is set
		//__ if the droid was produced in a factory. It is not triggered for droid theft or
		//__ gift (check ```eventObjectTransfer``` for that).
		//__
		virtual bool handle_eventDroidBuilt(const Droid* psDroid, optional<const Structure*> psFactory) = 0;

		//__ ## eventStructureBuilt(structure[, droid])
		//__
		//__ An event that is run every time a structure is produced. The droid parameter is set
		//__ if the structure was built by a droid. It is not triggered for building theft
		//__ (check ```eventObjectTransfer``` for that).
		//__
		virtual bool handle_eventStructureBuilt(const Structure* psStruct, optional<const Droid*> psDroid) = 0;

		//__ ## eventStructureDemolish(structure[, droid])
		//__
		//__ An event that is run every time a structure begins to be demolished. This does
		//__ not trigger again if the structure is partially demolished.
		//__
		virtual bool handle_eventStructureDemolish(const Structure* psStruct, optional<const Droid*> psDroid) = 0;

		//__ ## eventStructureReady(structure)
		//__
		//__ An event that is run every time a structure is ready to perform some
		//__ special ability. It will only fire once, so if the time is not right,
		//__ register your own timer to keep checking.
		//__
		virtual bool handle_eventStructureReady(const Structure* psStruct) = 0;

		//__ ## eventStructureUpgradeStarted(structure)
		//__
		//__ An event that is run every time a structure starts to be upgraded.
		//__
		virtual bool handle_eventStructureUpgradeStarted(const Structure* psStruct) = 0;

		//__ ## eventAttacked(victim, attacker)
		//__
		//__ An event that is run when an object belonging to the script's controlling player is
		//__ attacked. The attacker parameter may be either a structure or a droid.
		//__
		virtual bool handle_eventAttacked(const BaseObject * psVictim, const BaseObject * psAttacker) = 0;

		//__ ## eventResearched(research, structure, player)
		//__
		//__ An event that is run whenever a new research is available. The structure
		//__ parameter is set if the research comes from a research lab owned by the
		//__ current player. If an ally does the research, the structure parameter will
		//__ be set to null. The player parameter gives the player it is called for.
		//__
		virtual bool handle_eventResearched(const researchResult& research,
                                        event_nullable_ptr<const Structure> psStruct, unsigned player) = 0;

		//__ ## eventDestroyed(object)
		//__
		//__ An event that is run whenever an object is destroyed. Careful passing
		//__ the parameter object around, since it is about to vanish!
		//__
		virtual bool handle_eventDestroyed(const BaseObject * psVictim) = 0;

		//__ ## eventPickup(feature, droid)
		//__
		//__ An event that is run whenever a feature is picked up. It is called for
		//__ all players / scripts.
		//__ Careful passing the parameter object around, since it is about to vanish! (3.2+ only)
		//__
		virtual bool handle_eventPickup(const Feature* psFeat, const Droid* psDroid) = 0;

		//__ ## eventObjectSeen(viewer, seen)
		//__
		//__ An event that is run sometimes when an object, which was marked by an object label,
		//__ which was reset through resetLabel() to subscribe for events, goes from not seen to seen.
		//__ An event that is run sometimes when an objectm  goes from not seen to seen.
		//__ First parameter is **game object** doing the seeing, the next the game
		//__ object being seen.
		virtual bool handle_eventObjectSeen(const BaseObject * psViewer, const BaseObject * psSeen) = 0;

		//__
		//__ ## eventGroupSeen(viewer, group)
		//__
		//__ An event that is run sometimes when a member of a group, which was marked by a group label,
		//__ which was reset through resetLabel() to subscribe for events, goes from not seen to seen.
		//__ First parameter is **game object** doing the seeing, the next the id of the group
		//__ being seen.
		//__
		virtual bool handle_eventGroupSeen(const BaseObject * psViewer, int groupId) = 0;

		//__ ## eventObjectTransfer(object, from)
		//__
		//__ An event that is run whenever an object is transferred between players,
		//__ for example due to a Nexus Link weapon. The event is called after the
		//__ object has been transferred, so the target player is in object.player.
		//__ The event is called for both players.
		//__
		virtual bool handle_eventObjectTransfer(const BaseObject * psObj, unsigned from) = 0;

		//__ ## eventChat(from, to, message)
		//__
		//__ An event that is run whenever a chat message is received. The ```from``` parameter is the
		//__ player sending the chat message. For the moment, the ```to``` parameter is always the script
		//__ player.
		//__
		virtual bool handle_eventChat(unsigned from, unsigned to, const char* message) = 0;

		//__ ## eventBeacon(x, y, from, to[, message])
		//__
		//__ An event that is run whenever a beacon message is received. The ```from``` parameter is the
		//__ player sending the beacon. For the moment, the ```to``` parameter is always the script player.
		//__ Message may be undefined.
		//__
		virtual bool handle_eventBeacon(int x, int y, unsigned from, unsigned to, optional<const char*> message) = 0;

		//__ ## eventBeaconRemoved(from, to)
		//__
		//__ An event that is run whenever a beacon message is removed. The ```from``` parameter is the
		//__ player sending the beacon. For the moment, the ```to``` parameter is always the script player.
		//__
		virtual bool handle_eventBeaconRemoved(unsigned from, unsigned to) = 0;

		//__ ## eventGroupLoss(gameObject, groupId, newSize)
		//__
		//__ An event that is run whenever a group becomes empty. Input parameter
		//__ is the about to be killed object, the group's id, and the new group size.
		//__
		//		// Since groups are entities local to one context, we do not iterate over them here.
		virtual bool handle_eventGroupLoss(const BaseObject * psObj, int group, int size) = 0;

		//__ ## eventArea<label>(droid)
		//__
		//__ An event that is run whenever a droid enters an area label. The area is then
		//__ deactived. Call resetArea() to reactivate it. The name of the event is
		//__ `eventArea${label}`.
		//__
		virtual bool handle_eventArea(const std::string& label, const Droid* psDroid) = 0;

		//__ ## eventDesignCreated(template)
		//__
		//__ An event that is run whenever a new droid template is created. It is only
		//__ run on the client of the player designing the template.
		//__
		virtual bool handle_eventDesignCreated(const DroidTemplate* psTemplate) = 0;

		//__ ## eventAllianceOffer(from, to)
		//__
		//__ An event that is called whenever an alliance offer is requested.
		//__
		virtual bool handle_eventAllianceOffer(unsigned from, unsigned to) = 0;

		//__ ## eventAllianceAccepted(from, to)
		//__
		//__ An event that is called whenever an alliance is accepted.
		//__
		virtual bool handle_eventAllianceAccepted(unsigned from, unsigned to) = 0;

		//__ ## eventAllianceBroken(from, to)
		//__
		//__ An event that is called whenever an alliance is broken.
		//__
		virtual bool handle_eventAllianceBroken(unsigned from, unsigned to) = 0;

	public:
		// MARK: Special input events

		//__ ## eventSyncRequest(req_id, x, y, obj_id, obj_id2)
		//__
		//__ An event that is called from a script and synchronized with all other scripts and hosts
		//__ to prevent desync from happening. Sync requests must be carefully validated to prevent
		//__ cheating!
		//__
		virtual bool handle_eventSyncRequest(unsigned from, int req_id, int x, int y, const BaseObject * psObj,
		                                     const BaseObject * psObj2) = 0;

		//__ ## eventKeyPressed(meta, key)
		//__
		//__ An event that is called whenever user presses a key in the game, not counting chat
		//__ or other pop-up user interfaces. The key values are currently undocumented.
		virtual bool handle_eventKeyPressed(int meta, int key) SCRIPTING_EVENT_NON_REQUIRED
	};

	enum class GlobalVariableFlags
	{
		None = 0,
		ReadOnly = 1 << 0,
		// binary 0001
		ReadOnlyUpdatedFromApp = 1 << 1,
		// binary 0010
		DoNotSave = 1 << 2,
		// binary 0100
	};

	inline GlobalVariableFlags operator |(GlobalVariableFlags lhs, GlobalVariableFlags rhs)
	{
		using T = std::underlying_type<GlobalVariableFlags>::type;
		return static_cast<GlobalVariableFlags>(static_cast<T>(lhs) | static_cast<T>(rhs));
	}

	inline GlobalVariableFlags& operator |=(GlobalVariableFlags& lhs, GlobalVariableFlags rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	inline GlobalVariableFlags operator&(GlobalVariableFlags lhs, GlobalVariableFlags rhs)
	{
		using T = std::underlying_type<GlobalVariableFlags>::type;
		return static_cast<GlobalVariableFlags>(static_cast<T>(lhs) & static_cast<T>(rhs));
	}

	inline GlobalVariableFlags& operator&=(GlobalVariableFlags& lhs, GlobalVariableFlags rhs)
	{
		lhs = lhs & rhs;
		return lhs;
	}

	class scripting_instance : public scripting_event_handling_interface
	{
	public:
		scripting_instance(unsigned player, std::string scriptName, std::string scriptPath);
		~scripting_instance() override;

	public:
		virtual bool readyInstanceForExecution() = 0;

	public:
		[[nodiscard]] const std::string& scriptName() const { return m_scriptName; }
		[[nodiscard]] const std::string& scriptPath() const { return m_scriptPath; }
		[[nodiscard]] unsigned player() const { return m_player; }
		[[nodiscard]] bool isHostAI() const;

	public:
		inline void setReceiveAllEvents(bool value) { m_isReceivingAllEvents = value; }
		[[nodiscard]] inline bool isReceivingAllEvents() const { return m_isReceivingAllEvents; }

	public:
		// Helpers for loading a file from the "context" of a scripting_instance
		class LoadFileSearchOptions
		{
		public:
			static const unsigned ScriptPath_FileNameOnlyBackwardsCompat = 0x00000001;
			static const unsigned ScriptPath = 0x00000002;
			static const unsigned DataDir = 0x00000004;
			static const unsigned ConfigScriptDir = 0x00000008;
			static const unsigned All = ScriptPath | DataDir | ConfigScriptDir;
			static const unsigned All_BackwardsCompat = ScriptPath_FileNameOnlyBackwardsCompat | All;
		};

		// Loads a file.
		// (Intended for use from implementations of things like "include" functions.)
		//
		// Lookup order is as follows (based on the value of `searchFlags`):
		// - 1.) The filePath is checked relative to the read-only data dir search paths (LoadFileSearchOptions::DataDir)
		// - 2.) The filePath is checked relative to "<user's config dir>/script/" (LoadFileSearchOptions::ConfigScriptDir)
		// - 3.) The filename *only* is checked relative to the main scriptPath (LoadFileSearchOptions::ScriptPath_FileNameOnlyBackwardsCompat) - for backwards-compat only
		// - 4.) The filePath is checked relative to the main scriptPath (LoadFileSearchOptions::ScriptPath)
		bool loadFileForInclude(const std::string& filePath, std::string& loadedFilePath, char** ppFileData,
		                        std::size_t* pFileSize, unsigned searchFlags = LoadFileSearchOptions::All) const;

	public:
		// event handling
		// - see `scripting_event_handling_interface`

	public:
		// save / restore state
		virtual bool saveScriptGlobals(nlohmann::json& result) = 0;
		virtual bool loadScriptGlobals(const nlohmann::json& result) = 0;

		virtual nlohmann::json saveTimerFunction(uniqueTimerID timerID, std::string timerName,
		                                         const timerAdditionalData* additionalParam) = 0;

		// recreates timer functions (and additional userdata) based on the information saved by the saveTimerFunction() method
		virtual std::tuple<TimerFunc, std::unique_ptr<timerAdditionalData>> restoreTimerFunction(
			const nlohmann::json& savedTimerFuncData) = 0;

	public:
		// get state for debugging
		virtual nlohmann::json debugGetAllScriptGlobals() = 0;

		enum class DebugSpecialStringType
		{
			TYPE_DESCRIPTION
		};

		virtual std::unordered_map<std::string, DebugSpecialStringType> debugGetScriptGlobalSpecialStringValues();

		virtual bool debugEvaluateCommand(const std::string& text) = 0;

	public:
		// output to debug log file
		void dumpScriptLog(const std::string& info);
		void dumpScriptLog(const std::string& info, int me) const;

	public:
		virtual void updateGameTime(unsigned gameTime) = 0;
		virtual void updateGroupSizes(int group, int size) = 0;

		// set "global" variables
		//
		// expects: a json object (keys ("variable names") -> values)
		//
		// as appropriate for this scripting_instance, modifies "global variables" that scripts can access
		// for each key in the json object, it sets the appropriate "global variable" to the associated value
		//
		// only modifies global variables for keys in the json object - if other global variables already exist
		// in this scripting_instance (ex. from a prior call to this function), they are maintained
		//
		// flags: - GlobalVariableFlags::ReadOnly - if supported by the scripting instance, should set constant / read-only variables
		//          that the script(s) themselves cannot modify (but may be updated by WZ via future calls to this function)
		//        - GlobalVariableFlags::DoNotSave - indicates that the global variable(s) should not be saved by saveScriptGlobals()
		virtual void setSpecifiedGlobalVariables(const nlohmann::json& variables,
		                                         GlobalVariableFlags flags = GlobalVariableFlags::ReadOnly |
			                                         GlobalVariableFlags::DoNotSave) = 0;

		virtual void setSpecifiedGlobalVariable(const std::string& name, const nlohmann::json& value,
		                                        GlobalVariableFlags flags = GlobalVariableFlags::ReadOnly |
			                                        GlobalVariableFlags::DoNotSave) = 0;

	private:
		unsigned m_player;
		std::string m_scriptName;
		std::string m_scriptPath;
		bool m_isReceivingAllEvents = false;
	};

	class execution_context_base
	{
	public:
		virtual ~execution_context_base();
	public:
		virtual void throwError(const char* expr, int line, const char* function) const = 0;
	};

	class execution_context : public execution_context_base
	{
	public:
		~execution_context() override;
	public:
		[[nodiscard]] virtual wzapi::scripting_instance* currentInstance() const = 0;
		[[nodiscard]] unsigned player() const;
		void set_isReceivingAllEvents(bool value) const;
		[[nodiscard]] bool get_isReceivingAllEvents() const;
		[[nodiscard]] virtual playerCallbackFunc getNamedScriptCallback(const WzString& func) const = 0;
		virtual void doNotSaveGlobal(const std::string& global) const = 0;
	};

	struct game_object_identifier
	{
		game_object_identifier() = default;

		explicit game_object_identifier(BaseObject const* psObj)
			  : id(psObj->getId()), player(psObj->playerManager->getPlayer())
		{
		}

		unsigned id = -1;
		unsigned player = -1;
	};

	struct droid_id_player
	{
		unsigned id = -1;
		unsigned player = -1;
	};

	struct reservedParam
	{
	};

	struct string_or_string_list
	{
		std::vector<std::string> strings;
	};

	struct va_list_treat_as_strings
	{
		std::vector<std::string> strings;
	};

	template <typename ContainedType>
	struct va_list
	{
		std::vector<ContainedType> va_list;
	};

	struct optional_position
	{
		bool valid;
		int x;
		int y;
	};

	struct specified_player
	{
		unsigned player = -1;
	};

	struct STRUCTURE_TYPE_or_statsName_string
	{
		STRUCTURE_TYPE type = STRUCTURE_TYPE::COUNT;
		std::string statsName;
	};

	struct object_request
	{
	public:
		object_request();
		explicit object_request(std::string label);
		object_request(int x, int y);
		object_request(unsigned player, unsigned id);
	public:
		enum class RequestType
		{
			INVALID_REQUEST,
			LABEL_REQUEST,
			MAPPOS_REQUEST,
			OBJECTID_REQUEST
		};

	public:
		[[nodiscard]] const std::string& getLabel() const;
		[[nodiscard]] scr_position getMapPosition() const;
		[[nodiscard]] std::tuple<int, int> getObjectIDRequest() const;
	public:
		RequestType requestType;
	private:
		std::string str;
		int val1 = -1;
		int val2 = -1;
		int val3 = -1;
	};

	struct label_or_position_values
	{
	private:
		int VERY_LOW_INVALID_POS_VALUE = -2;
	public:
		label_or_position_values() = default;

		explicit label_or_position_values(std::string label)
			: type(Type::Label_Request)
			  , label(std::move(label))
		{
		}

		label_or_position_values(int x1, int y1, optional<int> x2 = nullopt, optional<int> y2 = nullopt)
			: type(Type::Position_Values_Request)
			  , x1(x1), y1(y1), x2(x2), y2(y2)
		{
		}

	public:
		[[nodiscard]] bool isValid() const { return type != Type::Invalid_Request; }
		[[nodiscard]] bool isLabel() const { return type == Type::Label_Request; }
		[[nodiscard]] bool isPositionValues() const { return type == Type::Position_Values_Request; }
	public:
		enum Type
		{
			Invalid_Request,
			Label_Request,
			Position_Values_Request
		};

		Type type = Invalid_Request;

		int x1 = VERY_LOW_INVALID_POS_VALUE;
		int y1 = VERY_LOW_INVALID_POS_VALUE;
		optional<int> x2;
		optional<int> y2;
		std::string label;
	};

	// retVals
	struct no_return_value
	{
	};

	struct researchResult
	{
		researchResult() = default;

		researchResult(ResearchStats* psResearch, unsigned player)
			: psResearch(psResearch)
			, player(player)
		{
		}

		ResearchStats* psResearch = nullptr;
		unsigned player;
	};

	struct researchResults
	{
		std::vector<const ResearchStats*> resList;
		unsigned player;
	};

	template <typename T>
	struct returned_nullable_ptr
	{
	private:
		using TYPE_POINTER = T*;
		TYPE_POINTER pt;

	public:
		explicit returned_nullable_ptr(TYPE_POINTER _pt)
			: pt(_pt)
		{
		}

		returned_nullable_ptr()
			: pt(nullptr)
		{
		}

		explicit operator TYPE_POINTER&()
		{
			return pt;
		}

		explicit operator TYPE_POINTER() const
		{
			return pt;
		}

		explicit operator bool() const noexcept
		{
			return pt != nullptr;
		}
	};

	class GameEntityRules
	{
	public:
		typedef std::map<std::string, int> NameToTypeMap;

		GameEntityRules(unsigned player, unsigned index, NameToTypeMap nameToTypeMap)
			: player(player), index(index), propertyNameToTypeMap(std::move(nameToTypeMap))
		{
		}

	public:
		using value_type = nlohmann::json;
		[[nodiscard]] value_type getPropertyValue(const wzapi::execution_context_base& context,
																							const std::string& name) const;
		value_type setPropertyValue(const wzapi::execution_context_base& context,
																const std::string& name,
		                            const value_type& newValue);
	public:
		[[nodiscard]] NameToTypeMap::const_iterator begin() const
		{
			return propertyNameToTypeMap.cbegin();
		}

		[[nodiscard]] NameToTypeMap::const_iterator end() const
		{
			return propertyNameToTypeMap.cend();
		}

		[[nodiscard]] unsigned getPlayer() const { return player; }
		[[nodiscard]] unsigned getIndex() const { return index; }
	private:
		// context
		unsigned player = -1;
		unsigned index = 0;
		NameToTypeMap propertyNameToTypeMap;
	};

	class GameEntityRuleContainer
	{
	public:
		typedef std::string GameEntityName;
		typedef std::pair<GameEntityName, GameEntityRules> GameEntityRulesPair;
	public:
		GameEntityRuleContainer() = default;

		void addRules(const GameEntityName& statsName, GameEntityRules&& entityRules)
		{
			rules.emplace_back(GameEntityRulesPair{statsName, std::move(entityRules)});
			lookup_table[statsName] = rules.size() - 1;
		}

	public:
		GameEntityRules& operator[](const GameEntityName& statsName)
		{
			return rules.at(lookup_table.at(statsName)).second;
		}

		[[nodiscard]] std::vector<GameEntityRulesPair>::const_iterator begin() const
		{
			return rules.cbegin();
		}

		[[nodiscard]] std::vector<GameEntityRulesPair>::const_iterator end() const
		{
			return rules.cend();
		}

	private:
		std::vector<GameEntityRulesPair> rules;
		std::unordered_map<GameEntityName, size_t> lookup_table;
	};

	class PerPlayerUpgrades
	{
	public:
		typedef std::string GameEntityClass;
	public:
		explicit PerPlayerUpgrades(unsigned player)
			: player(player)
		{
		}

		void addGameEntity(const GameEntityClass& entityClass, GameEntityRuleContainer&& rulesContainer)
		{
			upgrades.emplace(entityClass, std::move(rulesContainer));
		}

	public:
		GameEntityRuleContainer& operator[](const GameEntityClass& entityClass)
		{
			return upgrades[entityClass];
		}

		[[nodiscard]] const GameEntityRuleContainer* find(const GameEntityClass& entityClass) const
		{
			auto it = upgrades.find(entityClass);
			if (it == upgrades.end()) {
				return nullptr;
			}
			return &(it->second);
		}

		[[nodiscard]] unsigned getPlayer() const { return player; }

		[[nodiscard]] std::map<GameEntityClass, GameEntityRuleContainer>::const_iterator begin() const
		{
			return upgrades.cbegin();
		}

		[[nodiscard]] std::map<GameEntityClass, GameEntityRuleContainer>::const_iterator end() const
		{
			return upgrades.cend();
		}

	private:
		std::map<GameEntityClass, GameEntityRuleContainer> upgrades;
		unsigned player = 0;
	};

#define MULTIPLAY_SYNCREQUEST_REQUIRED
#define MUTLIPLAY_UNSAFE
#define WZAPI_DEPRECATED
#define WZAPI_AI_UNSAFE

	std::string translate(WZAPI_PARAMS(std::string const& str));
	int syncRandom(WZAPI_PARAMS(unsigned limit));
	bool setAlliance(WZAPI_PARAMS(unsigned player1, unsigned player2, bool areAllies));
	no_return_value sendAllianceRequest(WZAPI_PARAMS(unsigned player2));
	bool orderDroid(WZAPI_PARAMS(Droid* psDroid, ORDER_TYPE order));
	bool orderDroidBuild(WZAPI_PARAMS(Droid* psDroid, ORDER_TYPE order, std::string structureName, int x, int y,
                                    optional<float> direction));
	bool setAssemblyPoint(WZAPI_PARAMS(Structure *psStruct, int x, int y));
	bool setSunPosition(WZAPI_PARAMS(float x, float y, float z));
	bool setSunIntensity(WZAPI_PARAMS(float ambient_r, float ambient_g, float ambient_b, float diffuse_r,
	                                  float diffuse_g, float diffuse_b, float specular_r, float specular_g,
	                                  float specular_b));
	bool setWeather(WZAPI_PARAMS(int weatherType));
	bool setSky(WZAPI_PARAMS(std::string textureFilename, float windSpeed, float scale));
	bool cameraSlide(WZAPI_PARAMS(float x, float y));
	bool cameraZoom(WZAPI_PARAMS(float viewDistance, float speed));
	bool cameraTrack(WZAPI_PARAMS(optional<Droid *> _droid));
	unsigned addSpotter(WZAPI_PARAMS(int x, int y, unsigned player, int range, bool radar, unsigned expiry));
	bool removeSpotter(WZAPI_PARAMS(unsigned spotterId));
	bool syncRequest(WZAPI_PARAMS(int req_id, int x, int y, optional<const BaseObject *> _psObj,
	                              optional<const BaseObject *> _psObj2));
	bool replaceTexture(WZAPI_PARAMS(std::string oldFilename, std::string newFilename));
	bool changePlayerColour(WZAPI_PARAMS(unsigned player, int colour));
	bool setHealth(WZAPI_PARAMS(BaseObject * psObject, int health));
	MULTIPLAY_SYNCREQUEST_REQUIRED
	bool useSafetyTransport(WZAPI_PARAMS(bool flag));
	bool restoreLimboMissionData(WZAPI_NO_PARAMS);
	unsigned getMultiTechLevel(WZAPI_NO_PARAMS);
	bool setCampaignNumber(WZAPI_PARAMS(int campaignNumber));
	int getMissionType(WZAPI_NO_PARAMS);
	bool getRevealStatus(WZAPI_NO_PARAMS);
	bool setRevealStatus(WZAPI_PARAMS(bool status));
	bool autoSave(WZAPI_NO_PARAMS);

	// horrible hacks follow -- do not rely on these being present!
	no_return_value hackNetOff(WZAPI_NO_PARAMS);
	no_return_value hackNetOn(WZAPI_NO_PARAMS);
	no_return_value hackAddMessage(WZAPI_PARAMS(std::string message, int messageType, unsigned player, bool immediate));
	no_return_value hackRemoveMessage(WZAPI_PARAMS(std::string message, int messageType, unsigned player));
	returned_nullable_ptr<const BaseObject> hackGetObj(WZAPI_PARAMS(int _objectType, unsigned player, unsigned id))
	WZAPI_DEPRECATED;
	no_return_value hackAssert(WZAPI_PARAMS(bool condition, va_list_treat_as_strings message));
	bool receiveAllEvents(WZAPI_PARAMS(optional<bool> enabled));
	no_return_value hackDoNotSave(WZAPI_PARAMS(std::string name));
	no_return_value hackPlayIngameAudio(WZAPI_NO_PARAMS);
	no_return_value hackStopIngameAudio(WZAPI_NO_PARAMS);
	no_return_value hackMarkTiles(WZAPI_PARAMS(optional<label_or_position_values> _tilePosOrArea));

	// General functions -- geared for use in AI scripts
	no_return_value dump(WZAPI_PARAMS(va_list_treat_as_strings strings));
	no_return_value debugOutputStrings(WZAPI_PARAMS(va_list_treat_as_strings strings));
	bool console(WZAPI_PARAMS(va_list_treat_as_strings strings));
	bool clearConsole(WZAPI_NO_PARAMS);
	bool structureIdle(WZAPI_PARAMS(const Structure *psStruct));
	std::vector<const Structure*> enumStruct(WZAPI_PARAMS(optional<unsigned> _player,
                                                        optional<STRUCTURE_TYPE_or_statsName_string> _structureType,
                                                        optional<int> _playerFilter));
	std::vector<const Structure*> enumStructOffWorld(WZAPI_PARAMS(optional<unsigned> _player,
                                                                optional<STRUCTURE_TYPE_or_statsName_string>
	                                                              _structureType, optional<unsigned> _playerFilter));
	std::vector<const Droid*> enumDroid(WZAPI_PARAMS(optional<unsigned> _player, optional<int> _droidType,
                                                   optional<unsigned> _playerFilter));
	std::vector<const Feature*> enumFeature(WZAPI_PARAMS(unsigned playerFilter, optional<std::string> _featureName));
	std::vector<scr_position> enumBlips(WZAPI_PARAMS(unsigned player));
	std::vector<const BaseObject *> enumSelected(WZAPI_NO_PARAMS_NO_CONTEXT);
	GATEWAY_LIST enumGateways(WZAPI_NO_PARAMS);
	researchResult getResearch(WZAPI_PARAMS(std::string researchName, optional<unsigned> _player));
	researchResults enumResearch(WZAPI_NO_PARAMS);
	std::vector<const BaseObject *> enumRange(WZAPI_PARAMS(int x, int y, int range, optional<unsigned> _playerFilter,
                                                              optional<bool> _seen));
	bool pursueResearch(WZAPI_PARAMS(const Structure *psStruct, string_or_string_list research));
	researchResults findResearch(WZAPI_PARAMS(std::string researchName, optional<unsigned> _player));
	int distBetweenTwoPoints(WZAPI_PARAMS(int x1, int y1, int x2, int y2));
	bool orderDroidLoc(WZAPI_PARAMS(Droid *psDroid, ORDER_TYPE order_, int x, int y));
	unsigned playerPower(WZAPI_PARAMS(unsigned player));
	int queuedPower(WZAPI_PARAMS(unsigned player));
	bool isStructureAvailable(WZAPI_PARAMS(std::string structureName, optional<unsigned> _player));
	optional<scr_position> pickStructLocation(WZAPI_PARAMS(const Droid *psDroid, std::string structureName, int startX,
                                                         int startY, optional<int> _maxBlockingTiles));
	bool droidCanReach(WZAPI_PARAMS(const Droid *psDroid, int x, int y));
	bool propulsionCanReach(WZAPI_PARAMS(std::string propulsionName, int x1, int y1, int x2, int y2));
	int terrainType(WZAPI_PARAMS(int x, int y));
	bool tileIsBurning(WZAPI_PARAMS(int x, int y));
	bool orderDroidObj(WZAPI_PARAMS(Droid *psDroid, ORDER_TYPE _order, BaseObject *psObj));
	bool buildDroid(WZAPI_PARAMS(Structure *psFactory, std::string templateName, string_or_string_list body,
                               string_or_string_list propulsion, reservedParam reserved1, reservedParam reserved2,
                               va_list<string_or_string_list> turrets));
	Droid const* addDroid(WZAPI_PARAMS(unsigned player, int x, int y, std::string const& templateName,
                                                           string_or_string_list body,
                                                           string_or_string_list propulsion, reservedParam reserved1,
                                                           reservedParam reserved2,
                                                           va_list<string_or_string_list> turrets));
	MUTLIPLAY_UNSAFE
	std::unique_ptr<const DroidTemplate> makeTemplate(WZAPI_PARAMS(unsigned player, std::string templateName,
                                                                 string_or_string_list body,
                                                                 string_or_string_list propulsion,
                                                                 reservedParam reserved1,
                                                                 va_list<string_or_string_list> turrets));
	bool addDroidToTransporter(WZAPI_PARAMS(game_object_identifier transporter, game_object_identifier droid));
	returned_nullable_ptr<const Feature> addFeature(WZAPI_PARAMS(std::string featureName, int x, int y))
	MUTLIPLAY_UNSAFE;
	bool componentAvailable(WZAPI_PARAMS(std::string const& componentType, optional<std::string> _componentName));
	bool isVTOL(WZAPI_PARAMS(const Droid *psDroid));
	bool safeDest(WZAPI_PARAMS(unsigned player, int x, int y));
	bool activateStructure(WZAPI_PARAMS(Structure *psStruct, optional<BaseObject *> _psTarget));
	bool chat(WZAPI_PARAMS(unsigned playerFilter, std::string const& message));
	bool addBeacon(WZAPI_PARAMS(int x, int y, unsigned playerFilter, optional<std::string> _message));
	bool removeBeacon(WZAPI_PARAMS(unsigned playerFilter));
	std::unique_ptr<const Droid> getDroidProduction(WZAPI_PARAMS(const Structure *_psFactory));
	int getDroidLimit(WZAPI_PARAMS(optional<unsigned> _player, optional<int> _droidType));
	int getExperienceModifier(WZAPI_PARAMS(unsigned player));
	bool setDroidLimit(WZAPI_PARAMS(unsigned player, int maxNumber, optional<int> _droidType));
	bool setCommanderLimit(WZAPI_PARAMS(unsigned player, int maxNumber));
	bool setConstructorLimit(WZAPI_PARAMS(unsigned player, int maxNumber));
	bool setExperienceModifier(WZAPI_PARAMS(unsigned player, int percent));
	std::vector<const Droid*> enumCargo(WZAPI_PARAMS(const Droid *psDroid));
	bool isSpectator(WZAPI_PARAMS(unsigned player));

	nlohmann::json getWeaponInfo(WZAPI_PARAMS(std::string const& weaponName)) WZAPI_DEPRECATED;

	// MARK: - Functions that operate on the current player only
	bool centreView(WZAPI_PARAMS(int x, int y));
	bool playSound(WZAPI_PARAMS(std::string sound, optional<int> _x, optional<int> _y, optional<int> _z));
	bool gameOverMessage(WZAPI_PARAMS(bool gameWon, optional<bool> _showBackDrop, optional<bool> _showOutro));

	// MARK: - Global state manipulation -- not for use with skirmish AI (unless you want it to cheat, obviously)
	bool setStructureLimits(WZAPI_PARAMS(std::string const& structureName, int limit, optional<unsigned> _player));
	bool applyLimitSet(WZAPI_NO_PARAMS);
	no_return_value setMissionTime(WZAPI_PARAMS(int _time));
	int getMissionTime(WZAPI_NO_PARAMS);
	no_return_value setReinforcementTime(WZAPI_PARAMS(int _time));
	no_return_value completeResearch(WZAPI_PARAMS(std::string const& researchName, optional<unsigned> _player,
	                                              optional<bool> _forceResearch));
	no_return_value completeAllResearch(WZAPI_PARAMS(optional<int> _player));
	bool enableResearch(WZAPI_PARAMS(std::string const& researchName, optional<unsigned> _player));
	no_return_value setPower(WZAPI_PARAMS(int power, optional<unsigned> _player));
	WZAPI_AI_UNSAFE
	no_return_value setPowerModifier(WZAPI_PARAMS(int powerModifier, optional<unsigned> _player));
	WZAPI_AI_UNSAFE
	no_return_value setPowerStorageMaximum(WZAPI_PARAMS(int powerMaximum, optional<unsigned> _player));
	WZAPI_AI_UNSAFE
	no_return_value extraPowerTime(WZAPI_PARAMS(int time, optional<unsigned> _player));
	no_return_value setTutorialMode(WZAPI_PARAMS(bool enableTutorialMode));
	no_return_value setDesign(WZAPI_PARAMS(bool allowDesignValue));
	bool enableTemplate(WZAPI_PARAMS(std::string const& _templateName));
	bool removeTemplate(WZAPI_PARAMS(std::string const& _templateName));
	no_return_value setMiniMap(WZAPI_PARAMS(bool visible));
	no_return_value setReticuleButton(WZAPI_PARAMS(int buttonId, std::string const& tooltip, std::string const& filename,
	                                               std::string const& filenameDown, optional<std::string> callbackFuncName));
	no_return_value setReticuleFlash(WZAPI_PARAMS(int buttonId, bool flash));
	no_return_value showReticuleWidget(WZAPI_PARAMS(int buttonId));
	no_return_value showInterface(WZAPI_NO_PARAMS);
	no_return_value hideInterface(WZAPI_NO_PARAMS);
	no_return_value enableStructure(WZAPI_PARAMS(std::string const& structureName, optional<unsigned> _player));
	no_return_value enableComponent(WZAPI_PARAMS(std::string const& componentName, unsigned player));
	no_return_value makeComponentAvailable(WZAPI_PARAMS(std::string const& componentName, unsigned player));
	bool allianceExistsBetween(WZAPI_PARAMS(unsigned player1, unsigned player2));
	bool removeStruct(WZAPI_PARAMS(Structure *psStruct)) WZAPI_DEPRECATED;
	bool removeObject(WZAPI_PARAMS(BaseObject *psObj, optional<bool> _sfx));
	no_return_value setScrollLimits(WZAPI_PARAMS(int x1, int y1, int x2, int y2));
	scr_area getScrollLimits(WZAPI_NO_PARAMS);
	returned_nullable_ptr<const Structure> addStructure(
		WZAPI_PARAMS(std::string const& structureName, unsigned player, int x, int y));
	unsigned int getStructureLimit(WZAPI_PARAMS(std::string const& structureName, optional<unsigned> _player));
	int countStruct(WZAPI_PARAMS(std::string const& structureName, optional<unsigned> _playerFilter));
	int countDroid(WZAPI_PARAMS(optional<DROID_TYPE> _droidType, optional<unsigned> _playerFilter));
	no_return_value loadLevel(WZAPI_PARAMS(std::string const& levelName));
	no_return_value setDroidExperience(WZAPI_PARAMS(Droid *psDroid, double experience));
	bool donateObject(WZAPI_PARAMS(BaseObject *psObject, unsigned player));
	bool donatePower(WZAPI_PARAMS(int amount, unsigned player));
	no_return_value setNoGoArea(WZAPI_PARAMS(int x1, int y1, int x2, int y2, unsigned playerFilter));
	no_return_value startTransporterEntry(WZAPI_PARAMS(int x, int y, unsigned player));
	no_return_value setTransporterExit(WZAPI_PARAMS(int x, int y, unsigned player));
	no_return_value setObjectFlag(WZAPI_PARAMS(BaseObject *psObj, int _flag, bool flagValue))
	MULTIPLAY_SYNCREQUEST_REQUIRED;
	no_return_value fireWeaponAtLoc(WZAPI_PARAMS(std::string const& weaponName, int x, int y, optional<unsigned> _player));
	no_return_value fireWeaponAtObj(WZAPI_PARAMS(std::string const& weaponName, BaseObject *psObj, optional<unsigned> _player));
	bool setUpgradeStats(WZAPI_BASE_PARAMS(unsigned player, const std::string& name, int type, unsigned index,
	                                       const nlohmann::json& newValue));
	nlohmann::json getUpgradeStats(WZAPI_BASE_PARAMS(unsigned player, const std::string& name, int type, unsigned index));
	bool transformPlayerToSpectator(WZAPI_PARAMS(unsigned player));

	// MARK: - Used for retrieving information to set up script instance environments
	nlohmann::json constructStatsObject();
	nlohmann::json getUsefulConstants();
	nlohmann::json constructStaticPlayerData();
	std::vector<PerPlayerUpgrades> getUpgradesObject();
	nlohmann::json constructMapTilesArray();
}

#endif
