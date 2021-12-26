//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_DIFFICULTY_H
#define WARZONE2100_DIFFICULTY_H

extern int player_modifier;
extern int enemy_modifier;

enum class DIFFICULTY_LEVEL
{
    EASY,
    NORMAL,
    HARD,
    INSANE
};
extern DIFFICULTY_LEVEL current_difficulty;

inline void set_difficulty(DIFFICULTY_LEVEL level)
{
  using enum DIFFICULTY_LEVEL;
  switch (level)
  {
    case EASY:
      player_modifier = 120;
      enemy_modifier = 100;
      break;
    case NORMAL:
      player_modifier = 100;
      enemy_modifier = 100;
      break;
    case HARD:
      player_modifier = 100;
      enemy_modifier = 110;
      break;
    case INSANE:
      player_modifier = 80;
      enemy_modifier = 120;
  }
  current_difficulty = level;
}

#endif //WARZONE2100_DIFFICULTY_H
