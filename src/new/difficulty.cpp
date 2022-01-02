//
// Created by Luna Nothard on 02/01/2022.
//

#include "difficulty.h"

void set_difficulty(DIFFICULTY_LEVEL level)
{
  using enum DIFFICULTY_LEVEL;
  switch (level) {
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
