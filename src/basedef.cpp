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
	Spacetime SimpleObject::getSpacetime() const noexcept
	{
		return {time, position, rotation};
	}

	SimpleObject::SimpleObject(unsigned id, unsigned player)
		: id{id}, player{player}
	{
	}

	const Position& SimpleObject::getPosition() const noexcept
	{
		return position;
	}

	const Rotation& SimpleObject::getRotation() const noexcept
	{
		return rotation;
	}

	unsigned SimpleObject::getPlayer() const noexcept
	{
		return player;
	}

  void SimpleObject::setPlayer(unsigned p) noexcept
  {
    player = p;
  }

  void SimpleObject::setPosition(Position pos)
  {
    position = pos;
  }

	unsigned SimpleObject::getId() const noexcept
	{
		return id;
	}

  const DisplayData& SimpleObject::getDisplayData() const noexcept
  {
    return *display;
  }

	void SimpleObject::setHeight(int height) noexcept
	{
		position.z = height;
	}

	void SimpleObject::setRotation(Rotation new_rotation) noexcept
	{
		rotation = new_rotation;
	}

  bool SimpleObject::isSelectable() const
  {
    return flags.test(static_cast<std::size_t>(OBJECT_FLAG::UNSELECTABLE));
  }

  uint8_t SimpleObject::visibleToSelectedPlayer() const
  {
    return visibleToPlayer(selectedPlayer);
  }

  uint8_t SimpleObject::visibleToPlayer(unsigned watcher) const
  {
    if (god_mode) {
      return UBYTE_MAX;
    }
    if (watcher >= MAX_PLAYERS) {
      return 0;
    }
    return visibilityState[watcher];
  }
}

int objectPositionSquareDiff(const Position& first,
                             const Position& second)
{
  Vector2i diff = (first - second).xy();
  return dot(diff, diff);
}

int objectPositionSquareDiff(const SimpleObject& first,
                             const SimpleObject& second)
{
  return objectPositionSquareDiff(first.getPosition(),
                                  second.getPosition());
}
