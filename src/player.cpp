//
// Created by Luna Nothard on 01/02/2022.
//

#include "lib/framework/frame.h"

#include "player.h"


struct Player::Impl
{
  unsigned id;
};

struct PlayerManager::Impl
{
  explicit Impl(unsigned player);

  unsigned player;
};

PlayerManager::PlayerManager(unsigned player)
        : pimpl{std::make_unique<Impl>(player)}
{
}

PlayerManager::PlayerManager(PlayerManager const& rhs)
        : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

PlayerManager &PlayerManager::operator=(PlayerManager const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

PlayerManager::Impl::Impl(unsigned player)
        : player{player}
{
}

void PlayerManager::setPlayer(unsigned plr)
{
  if (!pimpl) return;
  pimpl->player = plr;
}

unsigned PlayerManager::getPlayer() const
{
  return pimpl ? pimpl->player : 0;
}

bool PlayerManager::isSelectedPlayer() const
{
  return getPlayer() == selectedPlayer;
}
