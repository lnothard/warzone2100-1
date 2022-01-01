//
// Created by Luna Nothard on 31/12/2021.
//

#include "event.h"

std::string get_team_description(const SkirmishGame& info)
{
  using enum ALLIANCE_SETTING;
  if (info.alliance_setting == ALLIANCES_TEAMS ||
      info.alliance_setting == ALLIANCES_UNSHARED) {
    return {};
  }

  std::map<int, std::size_t> team_player_count;
  get_team_counts(team_player_count);

  if (team_player_count.size() <= 1) {
    // does not have multiple teams
    return {};
  }

  std::string team_description;
  for (const auto& team : team_player_count)
  {
    if (!team_description.empty()) {
      team_description += "v";
    }
    team_description += std::to_string(team.second);
  }
  return team_description;
}

void get_team_counts(std::map<int, std::size_t>& team_player_count)
{
  for (const auto& player : NetPlay.players)
  {
    if (player.ai == AI_CLOSED) {
      // slot closed -- skip
      continue;
    } else if (player.ai == AI_OPEN) {
      if (player.isSpectator) {
        // spectator slot -- skip
        continue;
      } else {
        // slot available
        team_player_count[player.team]++;
      }
    } else {
      // bot player
      team_player_count[player.team]++;
    }
  }
}
