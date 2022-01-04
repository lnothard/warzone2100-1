
/**
 * @file animation.h
 *
 */

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
    /// Set to 0 if not currently tracking
    std::size_t start_time = 0;

    int initial_value = 0;
    int target_value = 0;
    int target_delta = 0;
    int speed = 0;
    int current_value = 0;
    bool target_reached = false;
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
    std::size_t start_time = 0;
    std::size_t duration = 0;
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
int calculateRelativeAngle(unsigned from, unsigned to);

/**
 *
 * @param easing_func
 * @param progress
 * @return
 */
unsigned calculate_easing(EASING_FUNCTION easing_func, unsigned progress)

#endif //WARZONE2100_ANIMATION_H
