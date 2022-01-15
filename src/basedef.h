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

#include "baseobject.h"

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

/**
 * The base type specification inherited by all persistent
 * game entities
 */
class SimpleObject
{
public:
    virtual ~SimpleObject() = default;

    /* Accessors */
    [[nodiscard]] virtual Spacetime getSpacetime() const = 0;
    [[nodiscard]] virtual const Position& getPosition() const = 0;
    [[nodiscard]] virtual const Rotation& getRotation() const = 0;
    [[nodiscard]] virtual unsigned getPlayer() const = 0;
    [[nodiscard]] virtual unsigned getId() const = 0;
    [[nodiscard]] virtual std::size_t getTime() const = 0;
    [[nodiscard]] virtual const DisplayData& getDisplayData() const = 0;

    virtual void setTime(unsigned t) = 0;
    virtual void setPlayer(unsigned p) = 0;
    virtual void setHeight(int height) = 0;
    virtual void setRotation(Rotation newRotation) = 0;
    virtual void setPosition(Position pos) = 0;
    [[nodiscard]] virtual bool isSelectable() const = 0;
    [[nodiscard]] virtual uint8_t visibleToPlayer(unsigned watcher) const = 0;
    [[nodiscard]] virtual uint8_t visibleToSelectedPlayer() const = 0;
};

namespace Impl
{
    class SimpleObject : public virtual ::SimpleObject
    {
    public:
        SimpleObject(unsigned id, unsigned player);

        /* Accessors */
        [[nodiscard]] Spacetime getSpacetime() const noexcept final;
        [[nodiscard]] const Position& getPosition() const noexcept final;
        [[nodiscard]] const Rotation& getRotation() const noexcept final;
        [[nodiscard]] unsigned getPlayer() const noexcept final;
        [[nodiscard]] unsigned getId() const noexcept final;
        [[nodiscard]] std::size_t getTime() const noexcept final;
        [[nodiscard]] const DisplayData& getDisplayData() const noexcept final;

        void setTime(unsigned t) noexcept final;
        void setPlayer(unsigned p) noexcept final;
        void setHeight(int height) noexcept final;
        void setRotation(Rotation new_rotation) noexcept final;
        void setPosition(Position pos) final;
        [[nodiscard]] bool isSelectable() const override;
        [[nodiscard]] uint8_t visibleToPlayer(unsigned watcher) const final;
        [[nodiscard]] uint8_t visibleToSelectedPlayer() const final;
    private:
        unsigned id;
        unsigned player;
        std::size_t time = 0;
        Position position {0, 0, 0};
        Rotation rotation {0, 0, 0};
        std::unique_ptr<DisplayData> display;
    protected:
        /// UBYTE_MAX if visible, UBYTE_MAX/2 if radar blip,
        /// 0 if not visible
        std::array<uint8_t, MAX_PLAYERS> visibilityState;
        std::bitset< static_cast<std::size_t>(OBJECT_FLAG::COUNT) > flags;
    };
}

/// 4D spacetime coordinate and orientation
struct Spacetime
{
    Spacetime() = default;
    Spacetime(std::size_t time, Position position, Rotation rotation);

    std::size_t time = 0;
    Position position {0, 0, 0};
    Rotation rotation {0, 0,0};
};

int objectPositionSquareDiff(const Position& first, const Position& second);

int objectPositionSquareDiff(const SimpleObject& first, const SimpleObject& second);

#endif // __INCLUDED_BASEDEF_H__
