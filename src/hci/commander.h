#ifndef __INCLUDED_SRC_HCI_COMMANDER_INTERFACE_H__
#define __INCLUDED_SRC_HCI_COMMANDER_INTERFACE_H__

#include "objects_stats.h"

class CommanderController : public BaseObjectsController, public std::enable_shared_from_this<CommanderController>
{
public:
	StructureStats* getObjectStatsAt(size_t objectIndex) const override;

	Structure* getAssignedFactoryAt(size_t objectIndex) const;

	size_t objectsSize() const override
	{
		return commanders.size();
	}

	Droid* getObjectAt(size_t index) const override
	{
		ASSERT_OR_RETURN(nullptr, index < commanders.size(), "Invalid object index (%zu); max: (%zu)", index,
		                 commanders.size());
		return commanders[index];
	}

	bool findObject(std::function<bool (BaseObject*)> iteration) const override
	{
		return BaseObjectsController::findObject(commanders, iteration);
	}

	void updateData();
	bool showInterface() override;
	void refresh() override;
	void clearData() override;
	void displayOrderForm();

	Droid* getHighlightedObject() const override
	{
		return highlightedCommander;
	}

	void setHighlightedObject(BaseObject* object) override;

private:
	void updateCommandersList();
	std::vector<Droid*> commanders;
	static Droid* highlightedCommander;
};

#endif // __INCLUDED_SRC_HCI_COMMANDER_INTERFACE_H__
