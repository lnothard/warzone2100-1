//
// Created by Luna Nothard on 26/12/2021.
//

#ifndef WARZONE2100_ANIMATION_H
#define WARZONE2100_ANIMATION_H

#include <cstdlib>

enum class EASING_FUNCTION
{
    LINEAR,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT
};

class Animation
{
public:
    bool is_active() const;
private:
    EASING_FUNCTION easing_func;
    std::size_t start_time;
    std::size_t duration;
};

#endif //WARZONE2100_ANIMATION_H
