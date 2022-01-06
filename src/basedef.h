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
 * Definitions for the base object type.
 */

#ifndef __INCLUDED_BASEDEF_H__
#define __INCLUDED_BASEDEF_H__

#include <array>
#include <bitset>

#include "lib/framework/vector.h"

#include "baseobject.h"
#include "displaydef.h"
#include "statsdef.h"
#include "weapondef.h"

// the died flag for a droid is set to this when it gets
// added to the non-current list
#define NOT_CURRENT_LIST 1

struct TILEPOS
{
	UBYTE x, y, type;
};

enum class OBJECT_FLAG
{
  JAMMED_TILES,
  TARGETED,
  DIRTY,
  UNSELECTABLE,
  COUNT // MUST BE LAST
};

/**
 * The base type specification inherited by all persistent
 * game entities
 */
class SimpleObject
{
public:
    SimpleObject() = default;
    virtual ~SimpleObject() = default;
    SimpleObject(cost SimpleObject&) = delete;
    SimpleObject(SimpleObject&&) = delete;
    SimpleObject& operator=(const SimpleObject&) = delete;
    SimpleObject& operator=(SimpleObject&&) = delete;

    /* Accessors */
    [[nodiscard]] virtual Spacetime get_spacetime() const = 0;
    [[nodiscard]] virtual const Position& get_position() const = 0;
    [[nodiscard]] virtual const Rotation& get_rotation() const = 0;
    [[nodiscard]] virtual unsigned get_player() const = 0;
    [[nodiscard]] virtual unsigned get_id() const = 0;
    [[nodiscard]] virtual const DisplayData& get_display_data() const = 0;

    virtual void set_height(int height) = 0;
    virtual void set_rotation(Rotation new_rotation) = 0;
    [[nodiscard]] virtual bool is_selectable() const = 0;
    [[nodiscard]] virtual uint8_t visible_to_player(unsigned watcher) const = 0;
    [[nodiscard]] virtual uint8_t visible_to_selected_player() const = 0;
};

namespace Impl
{
    class SimpleObject : public virtual ::SimpleObject
    {
    public:
        SimpleObject(unsigned id, unsigned player);

        /* Accessors */
        [[nodiscard]] Spacetime get_spacetime() const noexcept final;
        [[nodiscard]] const Position& get_position() const noexcept final;
        [[nodiscard]] const Rotation& get_rotation() const noexcept final;
        [[nodiscard]] unsigned get_player() const noexcept final;
        [[nodiscard]] unsigned get_id() const noexcept final;
        [[nodiscard]] const DisplayData& get_display_data() const noexcept final;

        void set_height(int height) noexcept final;
        void set_rotation(Rotation new_rotation) noexcept final;
        [[nodiscard]] bool is_selectable() const override;
        [[nodiscard]] uint8_t visible_to_player(unsigned watcher) const final;
        [[nodiscard]] uint8_t visible_to_selected_player() const final;
    private:
        unsigned id;
        unsigned player;
        std::size_t time = 0;
        Position position {0, 0, 0};
        Rotation rotation {0, 0, 0};
        std::unique_ptr<DisplayData> display;
        std::bitset< static_cast<std::size_t>(OBJECT_FLAG::COUNT) > flags;

        /// UBYTE_MAX if visible, UBYTE_MAX/2 if radar blip, 0 if not visible
        std::array<uint8_t, MAX_PLAYERS> visibility_state;
    };
}

/// 4D spacetime coordinate and orientation
struct Spacetime
{
    Spacetime(std::size_t time, Position position, Rotation rotation);

    std::size_t time;
    Position position;
    Rotation rotation;
};

static inline bool isDead(const SimpleObject* psObj)
{
	// See objmem.c for comments on the NOT_CURRENT_LIST hack
	return (psObj->died > NOT_CURRENT_LIST);
}

inline int object_position_square_diff(const Position& first,
                                       const Position& second)
{
  Vector2i diff = (first - second).xy();
  return dot(diff, diff);
}

inline int object_position_square_diff(const SimpleObject& first,
                                       const SimpleObject& second)
{
  return object_position_square_diff(first.get_position(),
                                     second.get_position());
}

#endif // __INCLUDED_BASEDEF_H__
