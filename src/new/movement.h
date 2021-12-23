//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_MOVEMENT_H
#define WARZONE2100_MOVEMENT_H

#include "lib/framework/vector.h"

enum class MOVEMENT_STATE
{
	INACTIVE,
	NAVIGATE,
	TURN,
	PAUSE,
	POINT_TO_POINT,
	TURN_TO_TARGET,
	HOVER,
	WAIT_ROUTE,
	SHUFFLE
};

class Movement
{
public:
	[[nodiscard]] constexpr bool is_inactive() const noexcept
	{
		return state == INACTIVE;
	}

	[[nodiscard]] constexpr bool is_stationary() const noexcept
	{
		return state == INACTIVE || state == HOVER || state == SHUFFLE;
	}

	constexpr void stop_moving() noexcept
	{
		// if flying: state = HOVER;

		state = INACTIVE;
	}

	constexpr void stop_moving_instantly() noexcept
	{
		stop_moving();
		speed = 0;
	}

  [[nodiscard]] constexpr int get_vertical_speed() const noexcept
  {
    return vertical_speed;
  }

private:
	using enum MOVEMENT_STATE;

	MOVEMENT_STATE state;
	Vector2i destination;
	Vector2i origin;
	int speed;
  int vertical_speed;
};

#endif // WARZONE2100_MOVEMENT_H
