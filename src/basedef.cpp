//
// Created by luna on 08/12/2021.
//

#include "lib/framework/vector.h"

#include "basedef.h"
#include "display.h"
#include "visibility.h"


Spacetime::Spacetime(std::size_t time, Position position, Rotation rotation)
	: time{time}, position{position}, rotation{rotation}
{
}

namespace Impl
{
	PersistentObject::PersistentObject(unsigned id, unsigned player)
		: id{id}, player{player}
	{
	}

  PersistentObject::~PersistentObject()
  {
    visRemoveVisibility(this);

    #ifdef DEBUG
      // hopefully this will trigger an infinite loop
      // if someone uses the freed object
      psNext = this;
      psNextFunc = this;
    #endif
  }

  Spacetime BaseObject::getSpacetime() const noexcept
  {
    return {time, position, rotation};
  }

	const Position& BaseObject::getPosition() const noexcept
	{
		return position;
	}

	const Rotation& BaseObject::getRotation() const noexcept
	{
		return rotation;
	}

	unsigned PersistentObject::getPlayer() const noexcept
	{
		return player;
	}

  unsigned BaseObject::getTime() const noexcept
  {
    return time;
  }

  Spacetime BaseObject::getPreviousLocation() const
  {
    return previousLocation;
  }

  void BaseObject::setTime(unsigned t) noexcept
  {
    time = t;
  }

  void PersistentObject::setPlayer(unsigned p) noexcept
  {
    player = p;
  }

  void BaseObject::setPosition(Position pos)
  {
    position = pos;
  }

	unsigned PersistentObject::getId() const noexcept
	{
		return id;
	}

  const DisplayData& BaseObject::getDisplayData() const noexcept
  {
    return *display;
  }

	void PersistentObject::setHeight(int height) noexcept
	{
		position.z = height;
	}

	void BaseObject::setRotation(Rotation new_rotation) noexcept
	{
		rotation = new_rotation;
	}

  bool PersistentObject::isSelectable() const
  {
    return flags.test(static_cast<std::size_t>(OBJECT_FLAG::UNSELECTABLE));
  }

  uint8_t PersistentObject::visibleToSelectedPlayer() const
  {
    return visibleToPlayer(selectedPlayer);
  }

  uint8_t PersistentObject::visibleToPlayer(unsigned watcher) const
  {
    if (godMode) {
      return UBYTE_MAX;
    }
    if (watcher >= MAX_PLAYERS) {
      return 0;
    }
    return visibilityState[watcher];
  }

  unsigned PersistentObject::getHp() const noexcept
  {
    return hitPoints;
  }

  void PersistentObject::setHp(unsigned hp)
  {
    hitPoints = hp;
  }
}

int objectPositionSquareDiff(const Position& first, const Position& second)
{
  Vector2i diff = (first - second).xy();
  return dot(diff, diff);
}

int objectPositionSquareDiff(const BaseObject& first, const BaseObject& second)
{
  return objectPositionSquareDiff(first.getPosition(), second.getPosition());
}
