//
// Created by Luna Nothard on 01/02/2022.
//

#ifndef WARZONE2100_PLAYER_H
#define WARZONE2100_PLAYER_H

#include <memory>


class Player
{
public:
  explicit Player(unsigned id);
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

class PlayerManager
{
public:
  ~PlayerManager() = default;
  explicit PlayerManager(unsigned player);

  PlayerManager(PlayerManager const& rhs);
  PlayerManager & operator=(PlayerManager const& rhs);

  PlayerManager(PlayerManager && rhs) noexcept = default;
  PlayerManager & operator=(PlayerManager && rhs) noexcept = default;

  void setPlayer(unsigned plr);
  [[nodiscard]] unsigned getPlayer() const;
  [[nodiscard]] bool isSelectedPlayer() const;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

#endif//WARZONE2100_PLAYER_H
