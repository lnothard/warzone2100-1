//
// Created by luna on 08/12/2021.
//

#include "lib/framework/vector.h"

#include "basedef.h"
#include "displaydef.h"

bool godMode;
void visRemoveVisibility(PersistentObject*);


Spacetime::Spacetime(unsigned time, Position position, Rotation rotation)
	: time{time}, position{position}, rotation{rotation}
{
}

struct BaseObject::Impl
{
  Impl() = default;
  ~Impl() = default;

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;

  unsigned time = 0;
  Position position {0, 0, 0};
  Rotation rotation {0, 0, 0};
  Spacetime previousLocation;
  std::unique_ptr<DisplayData> display;
};

BaseObject::Impl::Impl(Impl const& rhs)
  : display{rhs.display
      ? std::make_unique<DisplayData>(*rhs.display)
      : nullptr},
    time{rhs.time},
    position{rhs.position},
    rotation{rhs.rotation},
    previousLocation{rhs.previousLocation}
{
}

BaseObject::Impl& BaseObject::Impl::operator=(Impl const& rhs)
{
  display = rhs.display
            ? std::make_unique<DisplayData>(*rhs.display)
            : nullptr;
  time = rhs.time;
  position = rhs.position;
  rotation = rhs.rotation;
  previousLocation = rhs.previousLocation;
}

BaseObject::BaseObject(BaseObject const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

BaseObject& BaseObject::operator=(BaseObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

Spacetime BaseObject::getSpacetime() const noexcept
{
  return pimpl
         ? Spacetime(pimpl->time, pimpl->position, pimpl->rotation)
         : Spacetime();
}

Position BaseObject::getPosition() const noexcept
{
  return pimpl
         ? pimpl->position
         : Position();
}

Rotation BaseObject::getRotation() const noexcept
{
  return pimpl
         ? pimpl->rotation
         : Rotation();
}

unsigned BaseObject::getTime() const noexcept
{
  return pimpl
         ? pimpl->time
         : 0;
}

Spacetime BaseObject::getPreviousLocation() const noexcept
{
  return pimpl
         ? pimpl->previousLocation
         : Spacetime();
}

const DisplayData* BaseObject::getDisplayData() const noexcept
{
  return pimpl
         ? pimpl->display.get()
         : nullptr;
}

void BaseObject::setTime(unsigned t) noexcept
{
  if (!pimpl) return;
  pimpl->time = t;
}

void BaseObject::setPosition(Position pos) noexcept
{
  if (!pimpl) return;
  pimpl->position = pos;
}

void BaseObject::setRotation(Rotation new_rotation) noexcept
{
  if (!pimpl) return;
  pimpl->rotation = new_rotation;
}

void BaseObject::setHeight(int height) noexcept
{
  if (!pimpl) return;
  pimpl->position.z = height;
}

struct PersistentObject::Impl
{
    Impl(unsigned id, unsigned player);
    ~Impl() = default;

    Impl(Impl const& rhs) = default;
    Impl& operator=(Impl const& rhs) = default;

    Impl(Impl&& rhs) noexcept = default;
    Impl& operator=(Impl&& rhs) noexcept = default;

    std::bitset<static_cast<size_t>(OBJECT_FLAG::COUNT)> flags;
    unsigned id;
    unsigned player;
    unsigned bornTime;
    unsigned timeOfDeath = 0;
    unsigned periodicalDamage;
    unsigned periodicalDamageStartTime;
    unsigned hitPoints = 0;
    bool isSelected = false;
    /// UBYTE_MAX if visible, UBYTE_MAX/2 if radar blip, 0 if not visible
    std::array<uint8_t, MAX_PLAYERS> visibilityState;
    std::array<uint8_t, MAX_PLAYERS> seenThisTick;
};

PersistentObject::Impl::Impl(unsigned id, unsigned player)
  : id{id}, player{player}
{
}

PersistentObject::~PersistentObject()
{
  visRemoveVisibility(this);
}

PersistentObject::PersistentObject(unsigned id, unsigned player)
  : pimpl{std::make_unique<Impl>(id, player)}
{
}

PersistentObject::PersistentObject(PersistentObject const& rhs)
  : BaseObject(rhs),
    pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

PersistentObject& PersistentObject::operator=(PersistentObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

unsigned PersistentObject::getPlayer() const noexcept
{
  return pimpl
         ? pimpl->player
         : 0;
}

void PersistentObject::setPlayer(unsigned p) noexcept
{
  if (!pimpl) return;
  pimpl->player = p;
}

unsigned PersistentObject::getId() const noexcept
{
  return pimpl
         ? pimpl->id
         : 0;
}



bool PersistentObject::isDead() const noexcept
{
  return !pimpl || pimpl->timeOfDeath != 0;
}

bool PersistentObject::isSelectable() const
{
  return pimpl &&
    pimpl->flags.test(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE));
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
  if (watcher >= MAX_PLAYERS || !pimpl) {
    return 0;
  }
  return pimpl->visibilityState[watcher];
}

unsigned PersistentObject::getHp() const noexcept
{
  return pimpl
         ? pimpl->hitPoints
         : 0;
}

void PersistentObject::setHp(unsigned hp) noexcept
{
  if (!pimpl) return;
  pimpl->hitPoints = hp;
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
