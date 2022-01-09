#ifndef __INCLUDED_SRC_HCI_MANUFACTURE_INTERFACE_H__
#define __INCLUDED_SRC_HCI_MANUFACTURE_INTERFACE_H__

#include "objects_stats.h"

class ManufactureController : public BaseObjectsStatsController,
                              public std::enable_shared_from_this<ManufactureController>
{
public:
	DroidTemplate* getObjectStatsAt(size_t objectIndex) const override;

	DroidTemplate* getStatsAt(size_t statsIndex) const override
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
		updateManufactureOptionsList();
	}

	size_t objectsSize() const override
	{
		return factories.size();
	}

	Structure* getObjectAt(size_t index) const override
	{
		ASSERT_OR_RETURN(nullptr, index < factories.size(), "Invalid object index (%zu); max: (%zu)", index,
		                 factories.size());
		return factories[index];
	}

	bool findObject(std::function<bool (SimpleObject*)> iteration) const override
	{
		return BaseObjectsController::findObject(factories, iteration);
	}

	void updateData();
	void adjustFactoryProduction(DroidTemplate* manufactureOption, bool add) const;
	void adjustFactoryLoop(bool add) const;
	void releaseFactoryProduction(Structure* structure);
	void cancelFactoryProduction(Structure* structure);
	void startDeliveryPointPosition() const;
	bool showInterface() override;
	void refresh() override;
	void clearData() override;
	std::shared_ptr<StatsForm> makeStatsForm() override;

	Structure* getHighlightedObject() const override
	{
		return highlightedFactory;
	}

	void setHighlightedObject(SimpleObject* object) override;

private:
	void updateFactoriesList();
	void updateManufactureOptionsList();
	std::vector<DroidTemplate*> stats;
	std::vector<Structure*> factories;
	static Structure* highlightedFactory;
};

#endif // __INCLUDED_SRC_HCI_MANUFACTURE_INTERFACE_H__
