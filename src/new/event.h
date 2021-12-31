//
// Created by Luna Nothard on 31/12/2021.
//

#ifndef WARZONE2100_EVENT_H
#define WARZONE2100_EVENT_H

#include <string>

#include "lib/netplay/netplay.h"

static constexpr auto AI_CLOSED = -1;
static constexpr auto AI_OPEN = -2;

enum class GAME_MODE
{
    MENU,
    TUTORIAL,
    CAMPAIGN,
    CHALLENGE,
    SKIRMISH,
    HOSTING_IN_LOBBY,
    JOINING_IN_PROGRESS,
    /// joined but waiting on game information from host
    JOINING_IN_LOBBY,
    MULTIPLAYER
};

enum class GAME_END_REASON
{
    WON,
    LOST,
    QUIT
};

enum class ALLIANCE_SETTING
{
    /// FFA
    NO_ALLIANCES,
    /// Players can make and break alliances during the game.
    ALLIANCES,
    /// Alliances are set before the game.
    ALLIANCES_TEAMS,
    /// Alliances are set before the game. No shared research.
    ALLIANCES_UNSHARED,
};

/// Subclass EventHandler to implement a custom handler for
/// higher-level game-state event callbacks.
class EventHandler
{
    virtual ~EventHandler() = default;
};

struct SkirmishGame
{
    ALLIANCE_SETTING alliance_setting;
    std::vector<PLAYER> players;
};

enum class NET_PROTOCOL
{
    IPv4,
    IPv6
};

struct MultiplayerGame : public SkirmishGame
{
    NET_PROTOCOL protocol;
    std::string hostname;
    std::string lobby_address;
    unsigned lobby_port;
    unsigned id;
    bool private_lobby;
    bool host;
};

[[nodiscard]] std::string get_team_description(const SkirmishGame& info);
void get_team_counts(std::map<int, std::size_t>& team_player_count);

#endif //WARZONE2100_EVENT_H
