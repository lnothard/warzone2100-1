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

	void Simple_Object::set_height(int height) noexcept
	{
		position.z = height;
	}

	void Simple_Object::set_rotation(Rotation new_rotation) noexcept
	{
		rotation = new_rotation;
	}
}
