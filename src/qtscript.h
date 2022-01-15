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

#ifndef __INCLUDED_QTSCRIPT_H__
#define __INCLUDED_QTSCRIPT_H__

#include <unordered_set>

#include "lib/netplay/netplay.h"

#include "wzapi.h"

class WzString;
struct SimpleObject;
struct Droid;
struct DroidTemplate;
struct Feature;
struct ResearchStats;
struct Structure;

enum SCRIPT_TRIGGER_TYPE
{
	TRIGGER_GAME_INIT,
	TRIGGER_START_LEVEL,
	TRIGGER_TRANSPORTER_ARRIVED,
	TRIGGER_TRANSPORTER_LANDED,
	TRIGGER_TRANSPORTER_LAUNCH,
	TRIGGER_TRANSPORTER_EXIT,
	TRIGGER_TRANSPORTER_DONE,
	TRIGGER_DELIVERY_POINT_MOVING,
	TRIGGER_DELIVERY_POINT_MOVED,
	TRIGGER_VIDEO_QUIT,
	TRIGGER_MISSION_TIMEOUT,
	TRIGGER_GAME_LOADED,
	TRIGGER_GAME_SAVING,
	TRIGGER_GAME_SAVED,
	TRIGGER_DESIGN_BODY,
	TRIGGER_DESIGN_WEAPON,
	TRIGGER_DESIGN_COMMAND,
	TRIGGER_DESIGN_SYSTEM,
	TRIGGER_DESIGN_PROPULSION,
	TRIGGER_DESIGN_QUIT,
	TRIGGER_MENU_DESIGN_UP,
	TRIGGER_MENU_BUILD_UP,
	TRIGGER_MENU_BUILD_SELECTED,
	TRIGGER_MENU_MANUFACTURE_UP,
	TRIGGER_MENU_RESEARCH_UP,
	TRIGGER_MENU_RESEARCH_SELECTED,
	TRIGGER_OBJECT_RECYCLED
};

enum SCRIPT_TYPE
{
	SCRIPT_POSITION = OBJ_NUM_TYPES,
	SCRIPT_AREA,
	SCRIPT_PLAYER,
	SCRIPT_RESEARCH,
	SCRIPT_GROUP,
	SCRIPT_RADIUS,
	SCRIPT_COUNT
};

extern bool bInTutorial;

// ----------------------------------------------
// State functions

bool scriptInit();
void scriptSetStartPos(int position, int x, int y);
void scriptSetDerrickPos(int x, int y);
Vector2i getPlayerStartPosition(unsigned player);

/// Initialize script system
bool initScripts();

/// Shutdown script system
bool shutdownScripts();

/// Run after all data is loaded, but before game is started.
bool prepareScripts(bool loadGame);

/// Run this each logical frame to update frame-dependent script states
bool updateScripts();

// Load and evaluate the given script, kept in memory
bool loadGlobalScript(const WzString& path);
wzapi::scripting_instance* loadPlayerScript(const WzString& path, unsigned player, AIDifficulty difficulty);

// Set/write variables in the script's global context, run after loading script,
// but before triggering any events.
bool loadScriptStates(const char* filename);
bool saveScriptStates(const char* filename);

/// Tell script system that an object has been removed.
void scriptRemoveObject(const SimpleObject* psObj);

/// Open debug GUI
void jsShowDebug();

/// Choose a specific autogame AI
void jsAutogameSpecific(const WzString& name, unsigned player);

// ----------------------------------------------
// Event functions

/// For generic, parameter-less triggers, using an enum to avoid declaring a ton of parameter-less functions
bool triggerEvent(SCRIPT_TRIGGER_TYPE trigger, SimpleObject* psObj = nullptr);

// For each trigger with function parameters, a function to trigger it here
bool triggerEventDroidBuilt(Droid* psDroid, Structure* psFactory);
bool triggerEventAttacked(SimpleObject* psVictim, SimpleObject* psAttacker, int lastHit);
bool triggerEventResearched(ResearchStats* psResearch, Structure* psStruct, unsigned player);
bool triggerEventStructBuilt(Structure* psStruct, Droid* psDroid);
bool triggerEventStructDemolish(Structure* psStruct, Droid* psDroid);
bool triggerEventDroidIdle(Droid* psDroid);
bool triggerEventDestroyed(SimpleObject* psVictim);
bool triggerEventStructureReady(Structure* psStruct);
bool triggerEventStructureUpgradeStarted(Structure* psStruct);
bool triggerEventSeen(SimpleObject* psViewer, SimpleObject* psSeen);
bool triggerEventObjectTransfer(SimpleObject* psObj, unsigned from);
bool triggerEventChat(unsigned from, unsigned to, const char* message);
bool triggerEventBeacon(unsigned from, unsigned to, const char* message, int x, int y);
bool triggerEventBeaconRemoved(unsigned from, unsigned to);
bool triggerEventPickup(Feature* psFeat, Droid* psDroid);
bool triggerEventCheatMode(bool entered);
bool triggerEventGroupLoss(const SimpleObject* psObj, int group, int size, wzapi::scripting_instance* instance);
bool triggerEventDroidMoved(Droid* psDroid, int oldx, int oldy);
bool triggerEventArea(const std::string& label, Droid* psDroid);
bool triggerEventSelected();
bool triggerEventPlayerLeft(unsigned player);
bool triggerEventDesignCreated(DroidTemplate* psTemplate);
bool triggerEventSyncRequest(unsigned from, int req_id, int x, int y, SimpleObject* psObj, SimpleObject* psObj2);
bool triggerEventKeyPressed(int meta, int key);
bool triggerEventAllianceOffer(unsigned from, unsigned to);
bool triggerEventAllianceAccepted(unsigned from, unsigned to);
bool triggerEventAllianceBroken(unsigned from, unsigned to);

// ----------------------------------------------
// Debug functions

void jsDebugSelected(const SimpleObject* psObj);
void jsDebugMessageUpdate();

//

struct LABEL
{
	Vector2i p1 = Vector2i(0, 0); // world coordinates
	Vector2i p2 = Vector2i(0, 0); // world coordinates
	int id;
	int type;
	unsigned player;
	int subscriber;
	std::vector<int> idlist;
	int triggered;

	bool operator==(const LABEL& o) const
	{
		return id == o.id && type == o.type && player == o.player;
	}
};

struct generic_script_object
{
private:
	Vector2i p1 = {};
	Vector2i p2 = {};
	int id;
	unsigned player;
	int type;
public:
	generic_script_object();
	static generic_script_object Null();
	// all coordinates are in *map* coordinates
	static generic_script_object fromRadius(int x, int y, int radius);
	static generic_script_object fromArea(int x, int y, int x2, int y2);
	static generic_script_object fromPosition(int x, int y);
	static generic_script_object fromGroup(int groupId);
	static generic_script_object fromObject(const SimpleObject* psObj);
public:
	[[nodiscard]] inline bool isNull() const { return type < 0; }
	[[nodiscard]] inline bool isRadius() const { return type == SCRIPT_RADIUS; }
	[[nodiscard]] inline bool isArea() const { return type == SCRIPT_AREA; }
	[[nodiscard]] inline bool isPosition() const { return type == SCRIPT_POSITION; }
	[[nodiscard]] inline bool isGroup() const { return type == SCRIPT_GROUP; }
	[[nodiscard]] inline bool isObject() const { return type == OBJ_DROID || type == OBJ_FEATURE || type == OBJ_STRUCTURE; }
public:
	[[nodiscard]] inline int getType() const { return type; }
	[[nodiscard]] scr_radius getRadius() const; // if type == SCRIPT_RADIUS, returns the radius
	[[nodiscard]] scr_area getArea() const; // if type == SCRIPT_AREA, returns the area
	[[nodiscard]] scr_position getPosition() const; // if type == SCRIPT_POSITION, returns the position
	[[nodiscard]] int getGroupId() const; // if type == SCRIPT_GROUP, returns the groupId
	[[nodiscard]] SimpleObject* getObject() const; // if type == OBJ_DROID, OBJ_FEATURE, OBJ_STRUCTURE, returns the game object
public:
	[[nodiscard]] LABEL toNewLabel() const;
};

/// Load map labels
bool loadLabels(const char* filename);

/// Write map labels to savegame
bool writeLabels(const char* filename);

class scripting_engine
{
public:
	struct GROUPMAP
	{
		typedef int groupID;
		typedef std::unordered_map<const SimpleObject*, groupID> ObjectToGroupMap;
	private:
		ObjectToGroupMap m_map;
		typedef std::unordered_set<const SimpleObject*> GroupSet;
		std::unordered_map<groupID, GroupSet> m_groups;
		int lastNewGroupId = 0;
	protected:
		friend class scripting_engine;
		[[nodiscard]] int getLastNewGroupId() const { return lastNewGroupId; }
		void saveLoadSetLastNewGroupId(int value);
	public:
		groupID newGroupID();
		void insertObjectIntoGroup(const SimpleObject* psObj, groupID groupId);
		[[nodiscard]] inline const ObjectToGroupMap& map() const { return m_map; }
		[[nodiscard]] size_t groupSize(groupID groupId) const;
		optional<groupID> removeObjectFromGroup(const SimpleObject* psObj);
		[[nodiscard]] std::vector<const SimpleObject*> getGroupObjects(groupID groupId) const;
	};

	struct timerNode
	{
		uniqueTimerID timerID;
		TimerFunc function;
		std::string timerName;
		wzapi::scripting_instance* instance;
		int baseobj;
		OBJECT_TYPE baseobjtype;
		std::unique_ptr<timerAdditionalData> additionalTimerFuncParam;
		int frameTime;
		int ms;
		unsigned player;
		int calls;
		timerType type;

		timerNode() : instance(nullptr), baseobjtype(OBJ_NUM_TYPES), additionalTimerFuncParam(nullptr)
		{
		}

		timerNode(wzapi::scripting_instance* caller, TimerFunc  func, std::string  timerName, int plr,
		          int frame, std::unique_ptr<timerAdditionalData> additionalParam = nullptr);
		~timerNode();

		inline bool operator==(const timerNode& t)
		{
			return (timerID == t.timerID) && (timerName == t.timerName) && (player == t.player);
		}

		// implement operator less TODO

		timerNode(timerNode&& rhs) noexcept ; // move constructor

	private:
		// non-copyable
		timerNode(const timerNode&) = delete;
		timerNode& operator=(const timerNode&) = delete;

		void swap(timerNode& _rhs);
	};

private:
	typedef std::map<std::string, LABEL> LABELMAP;
	LABELMAP labels;

	typedef std::map<wzapi::scripting_instance*, GROUPMAP*> ENGINEMAP;
	ENGINEMAP groups;

	/// List of timer events for scripts. Before running them, we sort the list then run as many as we have time for.
	/// In this way, we implement load balancing of events and keep frame rates tidy for users. Since scripts run on the
	/// host, we do not need to worry about each peer simulating the world differently.
	std::list<std::shared_ptr<timerNode>> timers;
	uniqueTimerID lastTimerID = 0;
	std::unordered_map<uniqueTimerID, std::list<std::shared_ptr<timerNode>>::iterator> timerIDMap;
	// a map from uniqueTimerID -> entry in the timers list
private:
	scripting_engine() = default;

public:
	static scripting_engine& instance();
public:
	bool initScripts();
	bool prepareScripts(bool loadGame);
	bool updateScripts();
	bool shutdownScripts();

	wzapi::scripting_instance* loadPlayerScript(const WzString& path, unsigned player, AIDifficulty difficulty);

	// Set/write variables in the script's global context, run after loading script,
	// but before triggering any events.
	bool loadScriptStates(const char* filename);
	bool saveScriptStates(const char* filename);

	bool unregisterFunctions(wzapi::scripting_instance* instance);
	void prepareLabels();

	// MARK: LABELS
public:
	/// Load map labels
	bool loadLabels(const char* filename);

	/// Write map labels to savegame
	bool writeLabels(const char* filename);

	// MARK: GROUPS
public:
	GROUPMAP* getGroupMap(wzapi::scripting_instance* instance);

	bool loadGroup(wzapi::scripting_instance* instance, int groupId, int objId);
	bool saveGroups(nlohmann::json& result, wzapi::scripting_instance* instance);

	// MARK: TIMERS
public:
	uniqueTimerID setTimer(wzapi::scripting_instance* caller, const TimerFunc& timerFunc, unsigned player, int milliseconds,
	                       const std::string& timerName = "", const SimpleObject* obj = nullptr, timerType type = TIMER_REPEAT,
	                       std::unique_ptr<timerAdditionalData> additionalParam = nullptr);

	// removes any timer(s) that satisfy _pred
	template <class UnaryPredicate>
	std::vector<uniqueTimerID> removeTimersIf(UnaryPredicate _pred)
	{
		std::vector<uniqueTimerID> removedTimerIDs;
		timers.remove_if([_pred, &removedTimerIDs](const std::shared_ptr<timerNode>& node)
		{
			if (_pred(*node))
			{
				node->type = TIMER_REMOVED; // in case a timer is removed while running timers
				removedTimerIDs.push_back(node->timerID);
				return true;
			}
			return false;
		});
		for (const auto& timerID : removedTimerIDs)
		{
			timerIDMap.erase(timerID);
		}
		return removedTimerIDs;
	}

	bool removeTimer(uniqueTimerID timerID);
public:
	// Monitoring performance of function calls
	template <typename Func>
	void executeWithPerformanceMonitoring(wzapi::scripting_instance* instance, const std::string& function, Func f)
	{
		using microDuration = std::chrono::duration<uint64_t, std::micro>;
		auto time_begin = std::chrono::steady_clock::now();
		f(); // execute provided Func f
		auto duration_microsec = std::chrono::duration_cast<microDuration>(
			std::chrono::steady_clock::now() - time_begin);
		int ticks = duration_microsec.count();
		logFunctionPerformance(instance, function, ticks);
	}

private:
	static void logFunctionPerformance(wzapi::scripting_instance* instance, const std::string& function, int ticks);
	uniqueTimerID getNextAvailableTimerID();
	// internal-only function that adds a Timer node (used for restoring saved games)
	void addTimerNode(std::shared_ptr<timerNode>&& node);

	// MARK: triggering events (from wz game code)
public:
	static bool triggerEventSeen(SimpleObject* psViewer, SimpleObject* psSeen);

	// MARK: wzapi functions
public:
	// Used for retrieving information to set up script instance environments
	static nlohmann::json constructDerrickPositions();
	static nlohmann::json constructStartPositions();
public:
	// Label functions
	static wzapi::no_return_value resetLabel(WZAPI_PARAMS(std::string labelName, optional<int> playerFilter));
	static std::vector<std::string> enumLabels(WZAPI_PARAMS(optional<int> filterLabelType));

	static wzapi::no_return_value addLabel(WZAPI_PARAMS(generic_script_object object, std::string label,
	                                                    optional<int> _triggered));

	static int removeLabel(WZAPI_PARAMS(std::string label));
	static optional<std::string> getLabel(WZAPI_PARAMS(const SimpleObject *psObj));
	static optional<std::string> getLabelJS(WZAPI_PARAMS(wzapi::game_object_identifier obj_id));

	generic_script_object getObjectFromLabel(WZAPI_PARAMS(const std::string& label));
	wzapi::no_return_value hackMarkTiles_ByLabel(WZAPI_PARAMS(const std::string& label));
private:
	static optional<std::string> _findMatchingLabel(wzapi::game_object_identifier obj_id);
public:
	static generic_script_object getObject(WZAPI_PARAMS(wzapi::object_request request));
	static std::vector<const SimpleObject*> enumAreaByLabel(
		WZAPI_PARAMS(std::string label, optional<int> playerFilter, optional<bool> seen));
	static std::vector<const SimpleObject*> enumArea(
		WZAPI_PARAMS(scr_area area, optional<int> playerFilter, optional<bool> seen));
private:
	static std::vector<const SimpleObject*> _enumAreaWorldCoords(
		WZAPI_PARAMS(int x1, int y1, int x2, int y2, optional<int> playerFilter, optional<bool> seen));
public:
	// A special function for Javascript backends that accept either a label or a series of integers describing an area
	struct area_by_values_or_area_label_lookup
	{
	public:
		area_by_values_or_area_label_lookup();
		area_by_values_or_area_label_lookup(std::string  label);
		area_by_values_or_area_label_lookup(int x1, int y1, int x2, int y2);

		inline bool isLabel() { return m_isLabel; }
		inline optional<std::string> label() { return (m_isLabel) ? optional<std::string>(m_label) : nullopt; }

		inline optional<scr_area> area()
		{
			return (!m_isLabel) ? optional<scr_area>(scr_area{x1, y1, x2, y2}) : nullopt;
		}

	private:
		bool m_isLabel = false;
		std::string m_label;
		int x1 = -1;
		int y1 = -1;
		int x2 = -1;
		int y2 = -1;
	};

	static std::vector<const SimpleObject*> enumAreaJS(WZAPI_PARAMS(area_by_values_or_area_label_lookup area_lookup,
	                                                               optional<int> playerFilter, optional<bool> seen));

	// Group functions
	static std::vector<const SimpleObject*> enumGroup(WZAPI_PARAMS(int groupId));
	static int newGroup(WZAPI_NO_PARAMS);
	static wzapi::no_return_value groupAddArea(WZAPI_PARAMS(int groupId, int x1, int y1, int x2, int y2));
	static wzapi::no_return_value groupAddDroid(WZAPI_PARAMS(int groupId, const Droid *psDroid));
	static wzapi::no_return_value groupAdd(WZAPI_PARAMS(int groupId, const SimpleObject *psObj));
	static int groupSize(WZAPI_PARAMS(int groupId));
private:
	static wzapi::scripting_instance* findInstanceForPlayer(int match, const WzString& scriptName);

public:
	// debug APIs

	struct timerNodeSnapshot
	{
		uniqueTimerID timerID = 0;
		std::string timerName;
		wzapi::scripting_instance* instance = nullptr;
		int baseobj = -1;
		OBJECT_TYPE baseobjtype = OBJ_NUM_TYPES;
		int frameTime = -1;
		int ms = -1;
		unsigned player = -1;
		int calls = 0;
		timerType type = TIMER_REMOVED;
		nlohmann::json instanceTimerRestoreData;

		timerNodeSnapshot() = default;

		explicit timerNodeSnapshot(const std::shared_ptr<timerNode>& node)
		{
			ASSERT_OR_RETURN(, node != nullptr, "null timerNode");
			timerID = node->timerID;
			timerName = node->timerName;
			instance = node->instance;
			baseobj = node->baseobj;
			baseobjtype = node->baseobjtype;
			frameTime = node->frameTime;
			ms = node->ms;
			player = node->player;
			calls = node->calls;
			type = node->type;
			instanceTimerRestoreData = node->instance->saveTimerFunction(
				node->timerID, node->timerName, node->additionalTimerFuncParam.get());
		}
	};

	struct LabelInfo
	{
		WzString label;
		WzString type;
		WzString trigger;
		WzString owner;
		WzString subscriber;
	};

	class DebugInterface
	{
	protected:
		friend class scripting_engine;

		DebugInterface() = default;

	public:
		static std::unordered_map<wzapi::scripting_instance*, nlohmann::json> debug_GetGlobalsSnapshot() ;
		static std::vector<scripting_engine::timerNodeSnapshot> debug_GetTimersSnapshot() ;
		[[nodiscard]] static std::vector<scripting_engine::LabelInfo> debug_GetLabelInfo() ;
		/// Show all labels or all currently active labels
		static void markAllLabels(bool only_active);
		/// Mark and show label
		static void showLabel(const std::string& key, bool clear_old = true, bool jump_to = true);
	};

protected:
	friend void jsShowDebug();

	[[nodiscard]] std::unordered_map<wzapi::scripting_instance*, nlohmann::json> debug_GetGlobalsSnapshot() const;
	[[nodiscard]] std::vector<scripting_engine::timerNodeSnapshot> debug_GetTimersSnapshot() const;
	[[nodiscard]] std::vector<scripting_engine::LabelInfo> debug_GetLabelInfo() const;

	/// Show all labels or all currently active labels
	void markAllLabels(bool only_active);
	/// Mark and show label
	void showLabel(const std::string& key, bool clear_old = true, bool jump_to = true);

	friend bool triggerEventDroidMoved(Droid* psDroid, int oldx, int oldy);
	bool areaLabelCheck(Droid* psDroid);

	friend void scriptRemoveObject(const SimpleObject* psObj);
	void groupRemoveObject(const SimpleObject* psObj);

private:
	std::pair<bool, int> seenLabelCheck(wzapi::scripting_instance* instance, const SimpleObject* seen,
	                                    const SimpleObject* viewer);
	static void removeFromGroup(wzapi::scripting_instance* instance, GROUPMAP* psMap, const SimpleObject* psObj);
	bool groupAddObject(const SimpleObject* psObj, int groupId, wzapi::scripting_instance* instance);
};

/// Clear all map markers (used by label marking, for instance)
void clearMarks();

#endif
