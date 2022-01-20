/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file basedef.h
 * Definitions for the base object type
 */

#ifndef __INCLUDED_BASEDEF_H__
#define __INCLUDED_BASEDEF_H__

#include <array>
#include <bitset>

struct DisplayData;


enum class OBJECT_FLAG
{
  JAMMED_TILES,
  TARGETED,
  DIRTY,
  UNSELECTABLE,
  COUNT // MUST BE LAST
};

struct TILEPOS
{
  uint8_t x, y, type;
};

/// 4D spacetime coordinate and orientation
struct Spacetime
{
    Spacetime() = default;
    Spacetime(unsigned time, Position position, Rotation rotation);

    unsigned time = 0;
    Position position {0, 0, 0};
    Rotation rotation {0, 0,0};
};

/// The base type specification inherited by all game entities
class BaseObject
{
public:
    BaseObject() = default;
    virtual ~BaseObject() = default;

    BaseObject(BaseObject const& rhs);
    BaseObject& operator=(BaseObject const& rhs);

    BaseObject(BaseObject&& rhs) noexcept = default;
    BaseObject& operator=(BaseObject&& rhs) = default;

    [[nodiscard]] Spacetime getSpacetime() const noexcept;
    [[nodiscard]] Position getPosition() const noexcept;
    [[nodiscard]] Rotation getRotation() const noexcept;
    [[nodiscard]] unsigned getTime() const noexcept;
    [[nodiscard]] const DisplayData* getDisplayData() const noexcept;
    [[nodiscard]] Spacetime getPreviousLocation() const noexcept;
    void setTime(unsigned t) noexcept;
    void setRotation(Rotation newRotation) noexcept;
    void setPosition(Position pos) noexcept;
    void setHeight(int height) noexcept;
protected:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

class PersistentObject : public BaseObject
{
public:
    PersistentObject(unsigned id, unsigned player);
    ~PersistentObject() override;

    PersistentObject(PersistentObject const& rhs);
    PersistentObject& operator=(PersistentObject const& rhs);

    PersistentObject(PersistentObject&& rhs) noexcept = default;
    PersistentObject& operator=(PersistentObject&& rhs) = default;


    [[nodiscard]] unsigned getPlayer() const noexcept;
    [[nodiscard]] unsigned getId() const noexcept;
    [[nodiscard]] unsigned getHp() const noexcept;
    void setHp(unsigned hp) noexcept;
    void setPlayer(unsigned p) noexcept;
    [[nodiscard]] bool isDead() const noexcept;
    [[nodiscard]] bool isSelectable() const;
    [[nodiscard]] uint8_t visibleToPlayer(unsigned watcher) const;
    [[nodiscard]] uint8_t visibleToSelectedPlayer() const;

    [[nodiscard]] virtual bool isProbablyDoomed(bool isDirectDamage) const = 0;
    [[nodiscard]] virtual int objRadius() const = 0;
protected:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};


int objectPositionSquareDiff(const Position& first, const Position& second);
int objectPositionSquareDiff(const BaseObject* first, const BaseObject* second);

#endif // __INCLUDED_BASEDEF_H__
