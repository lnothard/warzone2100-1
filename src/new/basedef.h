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
	[[nodiscard]] virtual Spacetime getSpacetime() const = 0;
	[[nodiscard]] virtual const Position& getPosition() const = 0;
	[[nodiscard]] virtual const Rotation& getRotation() const = 0;
	[[nodiscard]] virtual unsigned getPlayer() const = 0;
	[[nodiscard]] virtual unsigned getId() const = 0;
	[[nodiscard]] virtual const DisplayData& getDisplayData() const = 0;

	virtual void setHeight(int height) = 0;
	virtual void setRotation(Rotation newRotation) = 0;
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
		[[nodiscard]] const DisplayData& getDisplayData() const noexcept final;

		void setHeight(int height) noexcept final;
		void setRotation(Rotation new_rotation) noexcept final;
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
    std::bitset< static_cast<std::size_t>(OBJECT_FLAG::COUNT) > flags;

    /// UBYTE_MAX if visible, UBYTE_MAX/2 if radar blip, 0 if not visible
    std::array<uint8_t, MAX_PLAYERS> visibilityState;
	};
}

inline int objectPositionSquareDiff(const Position& first, const Position& second)
{
	Vector2i diff = (first - second).xy();
	return dot(diff, diff);
}

inline int objectPositionSquareDiff(const SimpleObject& first, const SimpleObject& second)
{
	return objectPositionSquareDiff(first.getPosition(), second.getPosition());
}

#endif // WARZONE2100_BASEDEF_H
