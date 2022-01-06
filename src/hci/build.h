#ifndef __INCLUDED_SRC_HCI_BUILD_INTERFACE_H__
#define __INCLUDED_SRC_HCI_BUILD_INTERFACE_H__

#include "objects_stats.h"

class BuildController : public BaseObjectsStatsController, public std::enable_shared_from_this<BuildController>
{
public:
	StructureStats* getObjectStatsAt(size_t objectIndex) const override;

	StructureStats* getStatsAt(size_t statsIndex) const override
	{
		ASSERT_OR_RETURN(nullptr, statsIndex < stats.size(), "Invalid stats index (%zu); max: (%zu)", statsIndex,
		                 stats.size());
		return stats[statsIndex];
	}

	size_t statsSize() const override
	{
		return stats.size();
	}

	bool shouldShowRedundantDesign() const
	{
		return intGetShouldShowRedundantDesign();
	}

	void setShouldShowRedundantDesign(bool value)
	{
		intSetShouldShowRedundantDesign(value);
		updateBuildOptionsList();
	}

	bool shouldShowFavorites() const
	{
		return BuildController::showFavorites;
	}

	void setShouldShowFavorite(bool value)
	{
		BuildController::showFavorites = value;
		updateBuildOptionsList();
	}

	size_t objectsSize() const override
	{
		return builders.size();
	}

	Droid* getObjectAt(size_t index) const override
	{
		ASSERT_OR_RETURN(nullptr, index < builders.size(), "Invalid object index (%zu); max: (%zu)", index,
		                 builders.size());
		return builders[index];
	}

	bool findObject(std::function<bool (SimpleObject*)> iteration) const override
	{
		return BaseObjectsController::findObject(builders, iteration);
	}

	void updateData();
	void toggleFavorites(StructureStats* buildOption);
	void startBuildPosition(StructureStats* buildOption);
	bool showInterface() override;
	void refresh() override;
	void clearData() override;
	void toggleBuilderSelection(Droid* droid);
	std::shared_ptr<StatsForm> makeStatsForm() override;

	static void resetShowFavorites()
	{
		BuildController::showFavorites = false;
	}

	Droid* getHighlightedObject() const override
	{
		return highlightedBuilder;
	}

	void setHighlightedObject(SimpleObject* object) override;

private:
	void updateBuildersList();
	void updateBuildOptionsList();
	std::vector<StructureStats*> stats;
	std::vector<Droid*> builders;

	static bool showFavorites;
	static Droid* highlightedBuilder;
};

#endif // __INCLUDED_SRC_HCI_BUILD_INTERFACE_H__
