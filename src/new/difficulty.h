//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_DIFFICULTY_H
#define WARZONE2100_DIFFICULTY_H

///
extern int player_modifier;

///
extern int enemy_modifier;

///
enum class DIFFICULTY_LEVEL
{
    EASY,
    NORMAL,
    HARD,
    INSANE
};

/// The game difficulty is set globally
extern DIFFICULTY_LEVEL current_difficulty;

///
void set_difficulty(DIFFICULTY_LEVEL level);

#endif //WARZONE2100_DIFFICULTY_H
