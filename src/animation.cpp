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

#include "lib/gamelib/gtime.h"
#include "animation.h"


void ValueTracker::start(int value)
{
  initial = target = targetDelta = current = value;
  startTime = graphicsTime;
  targetReached = false;
}

void ValueTracker::stop()
{
  initial = current = startTime = 0;
  targetReached = false;
}

bool ValueTracker::isTracking() const
{
  return startTime != 0;
}

void ValueTracker::set_speed(int value)
{
	this->speed = value;
}

void ValueTracker::set_target_delta(int value)
{
	this->targetDelta = value;
	this->target = this->initial + value;
	this->targetReached = false;
}

void ValueTracker::set_target(int value)
{
  targetDelta = value - initial;
  target = value;
  targetReached = false;
}

void ValueTracker::update()
{
  if (targetReached || std::abs(target - current) < 1) {
    targetReached = true;
    return;
  }

  current = (initial + targetDelta - current) *
            static_cast<int>( realTimeAdjustedIncrement( static_cast<float>( speed )) ) + current;
}

int ValueTracker::get_current() const
{
	if (this->targetReached) {
		return this->target;
	}
	return static_cast<int>(this->current);
}

int ValueTracker::get_current_delta() const
{
	if (this->targetReached) {
		return this->targetDelta;
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
	return this->targetDelta;
}

bool ValueTracker::reachedTarget() const
{
	return this->targetReached;
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

template <class AnimatableData>
Animation<AnimatableData>::Animation(unsigned time)
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
	}
  else  {
		progress = UINT16_MAX;
	}

	currentData = initialData + (finalData - initialData) * (getEasedProgress() / (float)UINT16_MAX);
}

template <class AnimatableData>
bool Animation<AnimatableData>::isActive() const
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
	initialData = currentData = initial;
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
Animation<AnimatableData>& Animation<AnimatableData>::setDuration(unsigned durationMilliseconds)
{
	duration = static_cast<unsigned>(durationMilliseconds * 0.001 * GAME_TICKS_PER_SEC);
	return *this;
}

int calculateRelativeAngle(unsigned from, unsigned to)
{
  return static_cast<int>( to + (from - to) );
}

RotationAnimation::RotationAnimation(unsigned time)
  : Animation<Vector3f>{time}
{
}

void RotationAnimation::start()
{
	finalData = Vector3f((uint16_t)finalData.x, (uint16_t)finalData.y, (uint16_t)finalData.z);
	initialData = Vector3f(
          calculateRelativeAngle(static_cast<uint16_t>(initialData.x), static_cast<uint16_t>(finalData.x)),
          calculateRelativeAngle(static_cast<uint16_t>(initialData.y), static_cast<uint16_t>(finalData.y)),
          calculateRelativeAngle(static_cast<uint16_t>(initialData.z), static_cast<uint16_t>(finalData.z))
	);
	Animation::start();
}

template class Animation<Vector3f>;
template class Animation<float>;
