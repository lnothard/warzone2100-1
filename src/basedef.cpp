//
// Created by luna on 08/12/2021.
//

#include "lib/framework/vector.h"

#include "basedef.h"
#include "displaydef.h"

bool godMode;
void visRemoveVisibility(PlayerOwnedObject *);


Spacetime::Spacetime(unsigned time, Position position, Rotation rotation)
	: time{time}, position{position}, rotation{rotation}
{
}

struct BaseObject::Impl
{
  ~Impl() = default;
  explicit Impl(unsigned id);

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;

  unsigned id;
  unsigned time = 0;
  Position position {0, 0, 0};
  Rotation rotation {0, 0, 0};
  Spacetime previousLocation;
  std::unique_ptr<DisplayData> display;
};

BaseObject::BaseObject(unsigned id)
  : pimpl{std::make_unique<Impl>(id)}
{
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

BaseObject::Impl::Impl(unsigned id)
  : id{id}
{
}

BaseObject::Impl::Impl(Impl const& rhs)
  : id{rhs.id},
    display{rhs.display
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

unsigned BaseObject::getId() const noexcept
{
  return pimpl
         ? pimpl->id
         : 0;
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

struct PlayerOwnedObject::Impl
{
  Impl(unsigned id, unsigned player);

  unsigned id;
  unsigned player;
  unsigned bornTime = 0;
  unsigned timeOfDeath = 0;
  unsigned periodicalDamage = 0;
  unsigned periodicalDamageStartTime = 0;
  unsigned hitPoints = 0;
  bool isSelected = false;
  /// UBYTE_MAX if visible, UBYTE_MAX/2 if radar blip, 0 if not visible
  std::array<uint8_t, MAX_PLAYERS> visibilityState{};
  std::array<uint8_t, MAX_PLAYERS> seenThisTick{};
  std::bitset<static_cast<size_t>(OBJECT_FLAG::COUNT)> flags{};
};

PlayerOwnedObject::Impl::Impl(unsigned id, unsigned player)
  : id{id}, player{player}
{
  visibilityState.fill(0);
  seenThisTick.fill(0);
}

PlayerOwnedObject::~PlayerOwnedObject()
{
  visRemoveVisibility(this);
}

PlayerOwnedObject::PlayerOwnedObject(unsigned id, unsigned player)
  : BaseObject(id),
    pimpl{std::make_unique<Impl>(id, player)}
{
}

PlayerOwnedObject::PlayerOwnedObject(PlayerOwnedObject const& rhs)
  : BaseObject(rhs),
    pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

PlayerOwnedObject &PlayerOwnedObject::operator=(PlayerOwnedObject const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

unsigned PlayerOwnedObject::getPlayer() const noexcept
{
  return pimpl
         ? pimpl->player
         : 0;
}

void PlayerOwnedObject::setPlayer(unsigned p) noexcept
{
  if (!pimpl) return;
  pimpl->player = p;
}

bool PlayerOwnedObject::isDead() const noexcept
{
  return !pimpl || pimpl->timeOfDeath != 0;
}

bool PlayerOwnedObject::isSelectable() const
{
  return pimpl &&
    pimpl->flags.test(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE));
}

uint8_t PlayerOwnedObject::visibleToSelectedPlayer() const
{
  return visibleToPlayer(selectedPlayer);
}

uint8_t PlayerOwnedObject::visibleToPlayer(unsigned watcher) const
{
  if (godMode) {
    return UBYTE_MAX;
  }
  if (watcher >= MAX_PLAYERS || !pimpl) {
    return 0;
  }
  return pimpl->visibilityState[watcher];
}

unsigned PlayerOwnedObject::getHp() const noexcept
{
  return pimpl
         ? pimpl->hitPoints
         : 0;
}

void PlayerOwnedObject::setHp(unsigned hp) noexcept
{
  if (!pimpl) return;
  pimpl->hitPoints = hp;
}

int objectPositionSquareDiff(const Position& first, const Position& second)
{
  Vector2i diff = (first - second).xy();
  return dot(diff, diff);
}
