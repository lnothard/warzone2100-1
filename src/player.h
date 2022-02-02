//
// Created by Luna Nothard on 01/02/2022.
//

#ifndef WARZONE2100_PLAYER_H
#define WARZONE2100_PLAYER_H

#include <memory>

class ResourceExtractor;
class FlagPosition;


class Player
{
public:
  explicit Player(unsigned id);

  void addDroid(unsigned id, unsigned player);
  void addDroid(Droid& droid);
  [[nodiscard]] Droid* findDroidById(unsigned id) const;

  void addStructure(unsigned id, unsigned player);
  void addStructure(Structure& structure);
  void killStructure(Structure& structure);
  [[nodiscard]] Structure* findStructureById(unsigned id) const;

  void setPlayer(unsigned playerId);
  [[nodiscard]] unsigned getPlayer() const;
  [[nodiscard]] bool isSelectedPlayer() const;
public:
  unsigned id;
  std::vector<Droid> droids;
  std::vector<Structure> structures;
  std::vector<ResourceExtractor> extractors;
  std::vector<FlagPosition> flagPositions;
}; extern std::array<Player, MAX_PLAYERS> playerList;


template<typename T>
T* findById(unsigned id, std::vector<T> const& vec)
{
  auto it = std::find_if(vec.begin(), vec.end(),
                         [&id](auto const& t) {
    return t.getId() == id;
  });
  return it == vec.end() ? nullptr : &*it;
}

void killDroid(Droid& droid);

#endif//WARZONE2100_PLAYER_H
