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
	Spacetime Simple_Object::spacetime() const noexcept
	{
		return {time, position, rotation};
	}

	Simple_Object::Simple_Object(unsigned id, unsigned player)
		: id{id}, player{player}
	{
	}

	const Position& Simple_Object::get_position() const noexcept
	{
		return position;
	}

	const Rotation& Simple_Object::get_rotation() const noexcept
	{
		return rotation;
	}

	unsigned Simple_Object::get_player() const noexcept
	{
		return player;
	}

	unsigned Simple_Object::get_id() const noexcept
	{
		return id;
	}

  const Display_Data& Simple_Object::get_display_data() const noexcept
  {
    return display;
  }

	void Simple_Object::set_height(int height) noexcept
	{
		position.z = height;
	}

	void Simple_Object::set_rotation(Rotation new_rotation) noexcept
	{
		rotation = new_rotation;
	}

  bool Simple_Object::is_selectable() const
  {
    return flags.test(static_cast<std::size_t>(OBJECT_FLAG::UNSELECTABLE));
  }

  uint8_t Simple_Object::visible_to_selected_player() const
  {
    return visible_to_player(selectedPlayer);
  }

  uint8_t Simple_Object::visible_to_player(unsigned watcher) const
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
