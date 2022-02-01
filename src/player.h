//
// Created by Luna Nothard on 01/02/2022.
//

#ifndef WARZONE2100_PLAYER_H
#define WARZONE2100_PLAYER_H

#include <memory>

class Player {
public:
  ~Player() = default;
  explicit Player(unsigned player);

  Player(Player const& rhs);
  Player & operator=(Player const& rhs);

  Player(Player && rhs) noexcept = default;
  Player & operator=(Player && rhs) noexcept = default;

  void setPlayer(unsigned plr);
  [[nodiscard]] unsigned getPlayer() const;
  [[nodiscard]] bool isSelectedPlayer() const;
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

#endif//WARZONE2100_PLAYER_H
