//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_BASEDEF_H
#define WARZONE2100_BASEDEF_H

#include <array>
#include <bitset>

#include "lib/framework/vector.h"
#include "displaydef.h"

///
enum class OBJECT_FLAG
{
    JAMMED_TILES,
    TARGETED,
    DIRTY,
    UNSELECTABLE,
    COUNT // MUST BE LAST
};

/// 4D spacetime coordinate and rotation
struct Spacetime
{
	Spacetime(std::size_t time, Position position, Rotation rotation);

	std::size_t time;
	Position position;
	Rotation rotation;
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
	SimpleObject(const SimpleObject&) = delete;
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
		std::size_t time{0};
		Position position{Position(0, 0, 0)};
		Rotation rotation{Rotation(0, 0, 0)};
		std::unique_ptr<DisplayData> display;
    std::bitset< static_cast<std::size_t>(OBJECT_FLAG::COUNT) > flags;

    /// UBYTE_MAX if visible, UBYTE_MAX/2 if radar blip, 0 if not visible
    std::array<uint8_t, MAX_PLAYERS> visibility_state{};
	};
}

inline int object_position_square_diff(const Position& first, const Position& second)
{
	Vector2i diff = (first - second).xy();
	return dot(diff, diff);
}

inline int object_position_square_diff(const SimpleObject& first, const SimpleObject& second)
{
	return object_position_square_diff(first.get_position(), second.get_position());
}

#endif // WARZONE2100_BASEDEF_H
