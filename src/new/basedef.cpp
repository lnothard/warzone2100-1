//
// Created by luna on 08/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(std::size_t time, Position position, Rotation rotation)
	: time{time}, position{position}, rotation{rotation}
{
}

namespace Impl
{
	Spacetime SimpleObject::spacetime() const noexcept
	{
		return {time, position, rotation};
	}

	SimpleObject::SimpleObject(unsigned id, unsigned player)
		: id{id}, player{player}
	{
	}

	const Position& SimpleObject::get_position() const noexcept
	{
		return position;
	}

	const Rotation& SimpleObject::get_rotation() const noexcept
	{
		return rotation;
	}

	unsigned SimpleObject::get_player() const noexcept
	{
		return player;
	}

	unsigned SimpleObject::get_id() const noexcept
	{
		return id;
	}

  const Display_Data& SimpleObject::get_display_data() const noexcept
  {
    return *display;
  }

	void SimpleObject::set_height(int height) noexcept
	{
		position.z = height;
	}

	void SimpleObject::set_rotation(Rotation new_rotation) noexcept
	{
		rotation = new_rotation;
	}

  bool SimpleObject::is_selectable() const
  {
    return flags.test(static_cast<std::size_t>(OBJECT_FLAG::UNSELECTABLE));
  }

  uint8_t SimpleObject::visible_to_selected_player() const
  {
    return visible_to_player(selectedPlayer);
  }

  uint8_t SimpleObject::visible_to_player(unsigned watcher) const
  {
    if (god_mode) {
      return UBYTE_MAX;
    }
    if (watcher >= MAX_PLAYERS) {
      return 0;
    }
    return visibility_state[watcher];
  }
}
