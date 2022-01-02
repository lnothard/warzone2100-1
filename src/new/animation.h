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

class ValueTracker
{
public:
    void start(int value);
    void stop();
    void update();
    [[nodiscard]] bool currently_tracking() const;
    void set_target(int value);
private:
    std::size_t start_time;
    int initial_value;
    int target_value;
    int target_delta;
    int speed;
    float current_value;
    bool target_reached;
};

class Animation
{
public:
    virtual ~Animation() = default;
    [[nodiscard]] virtual bool is_active() const;
    virtual void start();
    virtual void update();
private:
    EASING_FUNCTION easing_func;
    std::size_t start_time{0};
    std::size_t duration{0};
    unsigned progress = UINT16_MAX;
};

class Rotation : public Animation
{
    void start() final;
};

/**
 * Find the angle equivalent to `from` in the interval between `to - 180°` and to `to + 180°`.
 *
 * For example:
 * - if `from` is `10°` and `to` is `350°`, it will return `370°`.
 * - if `from` is `350°` and `to` is `0°`, it will return `-10°`.
 *
 * Useful while animating a rotation, to always animate the shortest angle delta.
 */
int32_t calculateRelativeAngle(unsigned from, unsigned to);

unsigned calculate_easing(EASING_FUNCTION easing_func, unsigned progress)

#endif //WARZONE2100_ANIMATION_H
