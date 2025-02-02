#ifndef __INCLUDED_SRC_HCI_RESEARCH_INTERFACE_H__
#define __INCLUDED_SRC_HCI_RESEARCH_INTERFACE_H__

#include "objects_stats.h"

class ResearchController : public BaseObjectsStatsController, public std::enable_shared_from_this<ResearchController>
{
public:
	ResearchStats* getObjectStatsAt(size_t objectIndex) const override;

	ResearchStats* getStatsAt(size_t statsIndex) const override
	{
		ASSERT_OR_RETURN(nullptr, statsIndex < stats.size(), "Invalid stats index (%zu); max: (%zu)", statsIndex,
		                 stats.size());
		return stats[statsIndex];
	}

	size_t statsSize() const override
	{
		return stats.size();
	}

	size_t objectsSize() const override
	{
		return facilities.size();
	}

	Structure* getObjectAt(size_t index) const override
	{
		ASSERT_OR_RETURN(nullptr, index < facilities.size(), "Invalid object index (%zu); max: (%zu)", index,
		                 facilities.size());
		return facilities[index];
	}

	bool findObject(std::function<bool (BaseObject*)> iteration) const override
	{
		return BaseObjectsController::findObject(facilities, iteration);
	}

	nonstd::optional<size_t> getHighlightedFacilityIndex();
	void updateData();
	bool showInterface() override;
	void refresh() override;
	void clearData() override;
	std::shared_ptr<StatsForm> makeStatsForm() override;
	void startResearch(ResearchStats& research);
	void cancelResearch(Structure* facility);
	void requestResearchCancellation(Structure* facility);

	Structure* getHighlightedObject() const override
	{
		return highlightedFacility;
	}

	void setHighlightedObject(BaseObject* object) override;

private:
	void updateFacilitiesList();
	void updateResearchOptionsList();
	std::vector<ResearchStats*> stats;
	std::vector<Structure*> facilities;
	static Structure* highlightedFacility;
};

#endif // __INCLUDED_SRC_HCI_RESEARCH_INTERFACE_H__
