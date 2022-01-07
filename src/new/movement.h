
/**
 * @file movement.h
 */

#ifndef WARZONE2100_MOVEMENT_H
#define WARZONE2100_MOVEMENT_H

#include "lib/framework/vector.h"

#include "droid.h"

class Droid;

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

struct Movement
{
  Movement() = default;
  Movement(Vector2i origin, Vector2i destination);

	[[nodiscard]] bool is_inactive() const noexcept;
  [[nodiscard]] bool is_hovering() const noexcept;
	[[nodiscard]] bool isStationary() const noexcept;

  void move_droid_direct(Droid& droid, Vector2i position);

	void stop_moving() noexcept;
	void stop_moving_instantly() noexcept;
  void set_path_vars(int target_x, int target_y);

	using enum MOVEMENT_STATE;
	MOVEMENT_STATE state = INACTIVE;
  std::vector<Vector2i> path;
	Vector2i destination {0, 0};
	Vector2i origin {0, 0};
	int speed = 0;
  int vertical_speed = 0;
};

#endif // WARZONE2100_MOVEMENT_H
