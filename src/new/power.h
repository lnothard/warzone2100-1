//
// Created by Luna Nothard on 23/12/2021.
//

#ifndef WARZONE2100_POWER_H
#define WARZONE2100_POWER_H

static constexpr auto MAX_POWER = 1'000'000;

struct Player_Power
{
    int current;
    int modifier;
    int max_store;
    int total_extracted;
    int wasted;
};

#endif //WARZONE2100_POWER_H
