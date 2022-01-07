/*
	This file is part of Warzone 2100.
	Copyright (C) 2020  Warzone 2100 Project

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

/**
 * @file animation.cpp
 * Animation functions
 */

#include "animation.h"

void ValueTracker::start(int value)
{
  initial = value;
  target = value;
  target_delta = value;
  current = value;
  start_time = graphicsTime;
  target_reached = false;
}
//ValueTracker* ValueTracker::startTracking(int value)
//{
//	this->initial = value;
//	this->target = value;
//	this->targetDelta = 0;
//	this->current = value;
//	this->startTime = graphicsTime;
//	this->_reachedTarget = false;
//	return this;
//}

void ValueTracker::stop()
{
  initial = 0;
  current = 0;
  start_time = 0;
  target_reached = false;
}
//ValueTracker* ValueTracker::stopTracking()
//{
//	this->initial = 0;
//	this->current = 0;
//	this->startTime = 0;
//	this->_reachedTarget = false;
//	return this;
//}

bool ValueTracker::currently_tracking() const
{
  return start_time != 0;
}

void ValueTracker::set_speed(int value)
{
	this->speed = value;
}

void ValueTracker::set_target_delta(int value)
{
	this->target_delta = value;
	this->target = this->initial + value;
	this->target_reached = false;
}

void ValueTracker::set_target(int value)
{
  target_delta = value - initial;
  target = value;
  target_reached = false;
}

void ValueTracker::update()
{
  if (target_reached) {
    return;
  }
  if (std::abs(target - current) < 1) {
    target_reached = true;
    return;
  }

  current = (initial + target_delta - current) *
                  static_cast<int>( realTimeAdjustedIncrement(
                          static_cast<float>( speed )) )
                  + current;
}
//ValueTracker* ValueTracker::update()
//{
//	if (this->_reachedTarget)
//	{
//		return this;
//	}
//
//	if (std::abs(this->target - this->current) < 1)
//	{
//		this->_reachedTarget = true;
//		return this;
//	}
//
//	this->current = (this->initial + this->targetDelta - this->current) * realTimeAdjustedIncrement(this->speed) + this
//		->current;
//	return this;
//}

int ValueTracker::get_current() const
{
	if (this->target_reached)  {
		return this->target;
	}
	return static_cast<int>(this->current);
}

int ValueTracker::get_current_delta() const
{
	if (this->target_reached)  {
		return this->target_delta;
	}
	return static_cast<int>(this->current - this->initial);
}

int ValueTracker::get_initial() const
{
	return this->initial;
}

int ValueTracker::get_target() const
{
	return this->target;
}

int ValueTracker::get_target_delta() const
{
	return this->target_delta;
}

bool ValueTracker::reachedTarget() const
{
	return this->target_reached;
}

unsigned calculate_easing(EASING_FUNCTION easing_func, unsigned progress)
{
  using enum EASING_FUNCTION;
  switch (easing_func) {
    case LINEAR:
      return progress;
    case EASE_IN_OUT:
      return MAX(0, MIN(UINT16_MAX, iCos(UINT16_MAX / 2 + progress / 2)
                                    / 2 + (1 << 15)));
    case EASE_IN:
      return progress * progress / UINT16_MAX;
    case EASE_OUT:
      return 2 * progress - progress * progress / (UINT16_MAX - 1);
  }
}
//static uint16_t calculateEasing(EasingType easingType, uint16_t progress)
//{
//	switch (easingType)
//	{
//	case LINEAR:
//		return progress;
//	case EASE_IN_OUT:
//		return MAX(0, MIN(UINT16_MAX, iCos(UINT16_MAX / 2 + progress / 2) / 2 + (1 << 15)));
//	case EASE_IN:
//		return (progress * (uint32_t)progress) / UINT16_MAX;
//	case EASE_OUT:
//		return 2 * progress - (progress * (uint32_t)progress) / (UINT16_MAX - 1);
//	}
//
//	return UINT16_MAX;
//}

template <class AnimatableData>
Animation<AnimatableData>::Animation(std::size_t time)
  : time{time}
{
}

template <class AnimatableData>
void Animation<AnimatableData>::start()
{
	startTime = time;
	progress = 0;
}

template <class AnimatableData>
void Animation<AnimatableData>::update()
{
	if (duration > 0)  {
		auto deltaTime = time - (int64_t)startTime;
		progress = MAX(0, MIN(UINT16_MAX, UINT16_MAX * deltaTime / duration));
	} else  {
		progress = UINT16_MAX;
	}

	currentData = initialData + (finalData - initialData) * (getEasedProgress() / (float)UINT16_MAX);
}

template <class AnimatableData>
bool Animation<AnimatableData>::is_active() const
{
	return progress < UINT16_MAX;
}

template <class AnimatableData>
const AnimatableData& Animation<AnimatableData>::getCurrent() const
{
	return currentData;
}

template <class AnimatableData>
const AnimatableData& Animation<AnimatableData>::getFinalData() const
{
	return finalData;
}

template <class AnimatableData>
unsigned Animation<AnimatableData>::getEasedProgress() const
{
	return calculate_easing(easingType, progress);
}

template <class AnimatableData>
Animation<AnimatableData>& Animation<AnimatableData>::setInitialData(AnimatableData initial)
{
	initialData = initial;
	currentData = initial;
	return *this;
}

template <class AnimatableData>
Animation<AnimatableData>& Animation<AnimatableData>::setFinalData(AnimatableData final)
{
	finalData = final;
	return *this;
}

template <class AnimatableData>
Animation<AnimatableData>& Animation<AnimatableData>::setEasing(EASING_FUNCTION easing)
{
	easingType = easing;
	return *this;
}

template <class AnimatableData>
Animation<AnimatableData>& Animation<AnimatableData>::setDuration(uint32_t durationMilliseconds)
{
	duration = static_cast<uint32_t>(durationMilliseconds * 0.001 * GAME_TICKS_PER_SEC);
	return *this;
}

int calculate_relative_angle(unsigned from, unsigned to)
{
  return static_cast<int>( to + (from - to) );
}
/**
 * Find the angle equivalent to `from` in the interval between `to - 180°` and to `to + 180°`.
 *
 * For example:
 * - if `from` is `10°` and `to` is `350°`, it will return `370°`.
 * - if `from` is `350°` and `to` is `0°`, it will return `-10°`.
// *
// * Useful while animating a rotation, to always animate the shortest angle delta.
// */
//int32_t calculateRelativeAngle(uint16_t from, uint16_t to)
//{
//	return to + (int16_t)(from - to);
//}

RotationAnimation::RotationAnimation(std::size_t time)
  : Animation<Vector3f>{time}
{
}

void RotationAnimation::start()
{
	finalData = Vector3f((uint16_t)finalData.x, (uint16_t)finalData.y, (uint16_t)finalData.z);
	initialData = Vector3f(
          calculate_relative_angle(static_cast<uint16_t>(initialData.x), static_cast<uint16_t>(finalData.x)),
          calculate_relative_angle(static_cast<uint16_t>(initialData.y), static_cast<uint16_t>(finalData.y)),
          calculate_relative_angle(static_cast<uint16_t>(initialData.z), static_cast<uint16_t>(finalData.z))
	);
	Animation::start();
}

template class Animation<Vector3f>;
template class Animation<float>;
