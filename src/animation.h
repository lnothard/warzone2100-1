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
 * @file
 *
 * Definitions for animation functions.
 */

#include <cstdint>
#include "lib/gamelib/gtime.h"

#ifndef __INCLUDED_SRC_ANIMATION_H__
#define __INCLUDED_SRC_ANIMATION_H__

class ValueTracker
{
private:
  /// Set to 0 if not currently tracking
	std::size_t start_time = 0;

	int initial = 0;
	int target = 0;
  int current = 0;
	int target_delta = 0;
	bool target_reached = false;
	int speed = 10;
public:
	/// Starts the tracking with the specified initial value.
	void start(int value);
	/// Stops tracking
	void stop();
	/// Returns true if currently tracking a value.
	[[nodiscard]] bool currently_tracking() const;
	/// Sets speed/smoothness of the interpolation. 1 is syrup, 100 is instant. Default 10.
	ValueTracker* setSpeed(int value);
	/// Sets the target delta value
	ValueTracker* setTargetDelta(int value);
	/// Sets the absolute target value
	void set_target(int value);
	/// Update current value
	void update();
	/// Get initial value
	int getInitial();
	/// Get current value
	int getCurrent();
	/// Get current delta value
	int getCurrentDelta();
	/// Get absolute target value
	int getTarget();
	/// Get target delta value
	int getTargetDelta();
	/// Returns if the tracker reached its target
	bool reachedTarget();
};

enum class EASING_FUNCTION
{
	LINEAR,
	EASE_IN_OUT,
	EASE_IN,
	EASE_OUT,
};

template <class AnimatableData>
class Animation
{
public:
	explicit Animation(std::size_t time);
	virtual ~Animation() = default;

	virtual void start();
	void update();
	[[nodiscard]] bool is_active() const;
	[[nodiscard]] const AnimatableData& getCurrent() const;
	[[nodiscard]] const AnimatableData& getFinalData() const;
	Animation<AnimatableData>& setInitialData(AnimatableData initial);
	Animation<AnimatableData>& setFinalData(AnimatableData final);
	Animation<AnimatableData>& setEasing(EASING_FUNCTION easing);
	Animation<AnimatableData>& setDuration(uint32_t durationMilliseconds);

protected:
	[[nodiscard]] unsigned getEasedProgress() const;

  using enum EASING_FUNCTION;
	std::size_t time;
	EASING_FUNCTION easingType = LINEAR;
	unsigned duration = 0;
	unsigned startTime = 0;
	uint16_t progress = UINT16_MAX;
	AnimatableData initialData;
	AnimatableData finalData;
	AnimatableData currentData;
};

class RotationAnimation : public Animation<Vector3f>
{
public:
	explicit RotationAnimation(std::size_t time);

	void start() override;
};

#endif // __INCLUDED_SRC_ANIMATION_H__
