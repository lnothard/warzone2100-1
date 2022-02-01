//
// Created by Luna Nothard on 01/02/2022.
//

#include "lib/framework/frame.h"

#include "player.h"


struct Player::Impl
{
  explicit Impl(unsigned player);

  unsigned player = 0;
};

Player::Player(unsigned player)
        : pimpl{std::make_unique<Impl>(player)}
{
}

Player::Player(Player const& rhs)
        : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Player &Player::operator=(Player const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

Player::Impl::Impl(unsigned player)
        : player{player}
{
}

void Player::setPlayer(unsigned plr)
{
  if (!pimpl) return;
  pimpl->player = plr;
}

unsigned Player::getPlayer() const
{
  return pimpl ? pimpl->player : 0;
}

bool Player::isSelectedPlayer() const
{
  return getPlayer() == selectedPlayer;
}
