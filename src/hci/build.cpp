#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/widget/button.h"
#include "lib/widget/label.h"
#include "lib/widget/bar.h"
#include "build.h"

#include <utility>
#include "../objmem.h"
#include "../order.h"
#include "../qtscript.h"
#include "../power.h"
#include "../map.h"

Droid* BuildController::highlightedBuilder = nullptr;
bool BuildController::showFavorites = false;

void BuildController::updateData()
{
	updateBuildersList();
	updateHighlighted();
	updateBuildOptionsList();
}

void BuildController::updateBuildersList()
{
	builders.clear();

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "selectedPlayer = %" PRIu32 "", selectedPlayer);

	for (auto& droid : playerList[selectedPlayer].droids)
	{
		if (isConstructionDroid(droid) && droid->died == 0) {
			builders.push_back(droid);
		}
	}

	std::reverse(builders.begin(), builders.end());
}

void BuildController::updateBuildOptionsList()
{
	auto newBuildOptions = fillStructureList(selectedPlayer, MAXSTRUCTURES - 1, shouldShowFavorites());

	stats = std::vector<StructureStats*>(newBuildOptions.begin(), newBuildOptions.end());
}

StructureStats* BuildController::getObjectStatsAt(size_t objectIndex) const
{
	auto builder = getObjectAt(objectIndex);
	if (!builder)
	{
		return nullptr;
	}

	if (!(droidType(builder) == DROID_TYPE::CONSTRUCT ||
        droidType(builder) == DROID_TYPE::CYBORG_CONSTRUCT)) {
		return nullptr;
	}

	StructureStats* builderStats;
	if (orderStateStatsLoc(builder, ORDER_TYPE::BUILD, &builderStats)) // Moving to build location?
	{
		return builderStats;
	}

	if (builder->getOrder()->type == ORDER_TYPE::BUILD &&
      orderStateObj(builder, ORDER_TYPE::BUILD)) // Is building
	{
		return builder->getOrder()->structure_stats.get();
	}

	if (builder->getOrder()->type == ORDER_TYPE::HELP_BUILD ||
      builder->getOrder()->type == ORDER_TYPE::LINE_BUILD) // Is helping
	{
		if (auto structure = orderStateObj(builder, ORDER_TYPE::HELP_BUILD)) {
			return ((Structure*)structure)->getStats();
		}
	}

	if (orderState(builder, ORDER_TYPE::DEMOLISH))
	{
		return structGetDemolishStat();
	}

	return nullptr;
}


void BuildController::startBuildPosition(StructureStats* buildOption) const
{
	auto builder = getHighlightedObject();
	ASSERT_NOT_NULLPTR_OR_RETURN(, builder);

	triggerEvent(TRIGGER_MENU_BUILD_SELECTED);

	if (buildOption == structGetDemolishStat())
	{
		objMode = IOBJ_DEMOLISHSEL;
	}
	else
	{
		objMode = IOBJ_BUILDSEL;
		intStartConstructionPosition(builder, buildOption);
	}

	intRemoveStats();
	intMode = INT_OBJECT;
}

void BuildController::toggleFavorites(StructureStats* buildOption)
{
	asStructureStats[buildOption->index].is_favourite = !shouldShowFavorites();
	updateBuildOptionsList();
}

void BuildController::refresh()
{
	updateData();

	if (objectsSize() == 0)
	{
		closeInterface();
	}
}

void BuildController::clearData()
{
	builders.clear();
	setHighlightedObject(nullptr);
	stats.clear();
}
void BuildController::toggleBuilderSelection(Droid* droid)
{
	if (droid->damageManager->isSelected()) {
		droid->damageManager->setSelected(false);
	}
	else {
		if (auto highlightedObject = getHighlightedObject()) {
			highlightedObject->damageManager->setSelected(true);
		}
		selectObject(droid);
	}
	triggerEventSelected();
}

void BuildController::setHighlightedObject(BaseObject* object)
{
	if (object == nullptr) {
		highlightedBuilder = nullptr;
		return;
	}

	auto builder = dynamic_cast<Droid*>(object);
	ASSERT_NOT_NULLPTR_OR_RETURN(, builder);
	ASSERT_OR_RETURN(, isConstructionDroid(builder), "Droid is not a construction droid");
	highlightedBuilder = builder;
}

class BuildObjectButton : public ObjectButton
{
private:
	typedef ObjectButton BaseWidget;

public:
	BuildObjectButton(std::shared_ptr<BuildController>  controller, size_t newObjectIndex)
		: controller(std::move(controller))
	{
		objectIndex = newObjectIndex;
	}

	void clickPrimary() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);

		if (keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL) || keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT))
		{
			controller->toggleBuilderSelection(droid);
			return;
		}

		controller->clearSelection();
		controller->selectObject(controller->getObjectAt(objectIndex));
		jump();

		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);
		if (droid->damageManager->isDead()) {
			ASSERT_FAILURE(!isDead(droid), "!isDead(droid)", AT_MACRO, __FUNCTION__, "Droid is dead");
			// ensure the backing information is refreshed before the next draw
			intRefreshScreen();
			return;
		}
		displayIMD(Image(), ImdObject::Droid(droid), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

	BuildController& getController() const override
	{
		return *controller;
	}

	std::string getTip() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN("", droid);
		return droidGetName(droid);
	}

private:
	std::shared_ptr<BuildController> controller;
};

class BuildStatsButton : public StatsButton
{
private:
	typedef StatsButton BaseWidget;

protected:
	BuildStatsButton()
	= default;

public:
	static std::shared_ptr<BuildStatsButton> make(const std::shared_ptr<BuildController>& controller,
	                                              size_t objectIndex)
	{
		class make_shared_enabler : public BuildStatsButton
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->objectIndex = objectIndex;
		widget->initialize();
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto stat = getStats();
		displayIMD(Image(), stat ? ImdObject::StructureStat(stat) : ImdObject::Component(nullptr), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();
		auto droid = controller->getObjectAt(objectIndex);
		updateProgressBar(droid);
		updateProductionRunSizeLabel(droid);
	}

private:
	StructureStats* getStats() override
	{
		return controller->getObjectStatsAt(objectIndex);
	}

	void initialize()
	{
		addProgressBar();
		addProductionRunSizeLabel();
	}

	void addProductionRunSizeLabel()
	{
		W_LABINIT init;
		init.style = WIDG_HIDDEN;
		init.x = OBJ_TEXTX;
		init.y = OBJ_T1TEXTY;
		init.width = 16;
		init.height = 16;
		init.pText = WzString::fromUtf8("BUG! (a)");

		attach(productionRunSizeLabel = std::make_shared<W_LABEL>(&init));
		productionRunSizeLabel->setFontColour(WZCOL_ACTION_PRODUCTION_RUN_TEXT);
	}

	void updateProgressBar(Droid* droid)
	{
		progressBar->hide();

		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);
		if (!DroidIsBuilding(droid)) {
			return;
		}

		ASSERT(droid->getComponent(COMPONENT_TYPE::CONSTRUCT), "Invalid droid type");

		if (auto structure = DroidGetBuildStructure(droid)) {
			//show progress of build
			if (structure->getCurrentBuildPoints() != 0) {
				formatTime(progressBar.get(), structure->getCurrentBuildPoints(),
                   structureBuildPointsToCompletion(*structure),
				           structure->lastBuildRate, _("Build Progress"));
			}
			else {
				formatPower(progressBar.get(), checkPowerRequest(structure),
                    structure->getStats()->power_cost);
			}
		}
	}

	void updateProductionRunSizeLabel(Droid* droid)
	{
		int remaining = -1;

		StructureStats const* stats = nullptr;
		int count = 0;
		auto processOrder = [&](Order const& order)
		{
			StructureStats* newStats = nullptr;
			int deltaCount = 0;
			switch (order.type)
			{
			case ORDER_TYPE::BUILD:
			case ORDER_TYPE::LINE_BUILD:
				newStats = order.structure_stats.get();
				deltaCount = order.type == ORDER_TYPE::LINE_BUILD
					             ? 1 + (abs(order.pos.x - order.pos2.x) + abs(order.pos.y - order.pos2.y)) / TILE_UNITS
					             : 1;
				break;
			case ORDER_TYPE::HELP_BUILD:
				if (auto target = dynamic_cast<Structure*>(order.structure_stats.get())) {
					newStats = target->getStats();
					deltaCount = 1;
				}
				break;
			default:
				return false;
			}
			if (newStats != nullptr && (stats == nullptr || stats == newStats)) {
				stats = newStats;
				count += deltaCount;
				return true;
			}
			return false;
		};

		if (droid && processOrder(droid->order)) {
			for (auto const& order : droid->asOrderList)
			{
				if (!processOrder(order)) {
					break;
				}
			}
		}
		if (count > 1) {
			remaining = count;
		}

		if (remaining != -1) {
			productionRunSizeLabel->setString(WzString::fromUtf8(astringf("%d", remaining)));
			productionRunSizeLabel->show();
		}
		else {
			productionRunSizeLabel->hide();
		}
	}

	bool isHighlighted() const override
	{
		auto droid = controller->getObjectAt(objectIndex);
		return droid && (droid->damageManager->isSelected() ||
                     droid == controller->getHighlightedObject());
	}

	void clickPrimary() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);

		if (keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL) || keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT)) {
			controller->toggleBuilderSelection(droid);
		}
		else {
			controller->clearSelection();
			controller->selectObject(droid);
		}

		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

	void clickSecondary() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);
		auto highlighted = controller->getHighlightedObject();

		// prevent highlighting a builder when another builder is already selected
		if (droid == highlighted || !highlighted->damageManager->isSelected()) {
			controller->setHighlightedObject(droid);
			BaseStatsController::scheduleDisplayStatsForm(controller);
		}
	}

	std::shared_ptr<W_LABEL> productionRunSizeLabel;
	std::shared_ptr<BuildController> controller;
	size_t objectIndex;
};

class BuildOptionButton : public StatsFormButton
{
private:
	typedef StatsFormButton BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<BuildOptionButton> make(const std::shared_ptr<BuildController>& controller,
	                                               size_t buildOptionIndex)
	{
		class make_shared_enabler : public BuildOptionButton
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->buildOptionIndex = buildOptionIndex;
		widget->initialize();
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto stat = getStats();
		ASSERT_NOT_NULLPTR_OR_RETURN(, stat);

		displayIMD(Image(), ImdObject::StructureStat(stat), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

private:
	StructureStats* getStats() override
	{
		return controller->getStatsAt(buildOptionIndex);
	}

	void initialize()
	{
		addCostBar();
	}

	bool isHighlighted() const override
	{
		return controller->isHighlightedObjectStats(buildOptionIndex);
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();

		if (isMouseOverWidget())
		{
			intSetShadowPower(getCost());
		}

		costBar->majorSize = std::min(100, (int32_t)(getCost() / POWERPOINTS_DROIDDIV));
	}

	uint32_t getCost() override
	{
		StructureStats* psStats = getStats();
		return psStats ? psStats->power_cost : 0;
	}

	void clickPrimary() override
	{
		auto clickedStats = controller->getStatsAt(buildOptionIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, clickedStats);

		auto controllerRef = controller;
		widgScheduleTask([clickedStats, controllerRef]()
		{
			controllerRef->startBuildPosition(clickedStats);
		});
	}

	void clickSecondary() override
	{
		auto clickedStats = controller->getStatsAt(buildOptionIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, clickedStats);

		auto controllerRef = controller;
		widgScheduleTask([clickedStats, controllerRef]()
		{
			controllerRef->toggleFavorites(clickedStats);
		});
	}

	std::shared_ptr<BuildController> controller;
	size_t buildOptionIndex;
};

class BuildObjectsForm : public ObjectsForm
{
private:
	typedef ObjectsForm BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<BuildObjectsForm> make(const std::shared_ptr<BuildController>& controller)
	{
		class make_shared_enabler : public BuildObjectsForm
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->initialize();
		return widget;
	}

	std::shared_ptr<StatsButton> makeStatsButton(size_t buttonIndex) const override
	{
		return BuildStatsButton::make(controller, buttonIndex);
	}

	std::shared_ptr<ObjectButton> makeObjectButton(size_t buttonIndex) const override
	{
		return std::make_shared<BuildObjectButton>(controller, buttonIndex);
	}

protected:
	BuildController& getController() const override
	{
		return *controller;
	}

private:
	std::shared_ptr<BuildController> controller;
};

class BuildStatsForm : public ObjectStatsForm
{
private:
	typedef ObjectStatsForm BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<BuildStatsForm> make(const std::shared_ptr<BuildController>& controller)
	{
		class make_shared_enabler : public BuildStatsForm
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->initialize();
		return widget;
	}

	std::shared_ptr<StatsFormButton> makeOptionButton(size_t buttonIndex) const override
	{
		return BuildOptionButton::make(controller, buttonIndex);
	}

protected:
	BuildController& getController() const override
	{
		return *controller;
	}

private:
	void initialize() override
	{
		BaseWidget::initialize();
		addObsoleteButton();
		addFavoriteButton();
	}

	void addObsoleteButton()
	{
		attach(obsoleteButton = std::make_shared<MultipleChoiceButton>());
		obsoleteButton->style |= WBUT_SECONDARY;
		obsoleteButton->setChoice(controller->shouldShowRedundantDesign());
		obsoleteButton->setImages(false, MultipleChoiceButton::Images(Image(IntImages, IMAGE_OBSOLETE_HIDE_UP),
		                                                              Image(IntImages, IMAGE_OBSOLETE_HIDE_UP),
		                                                              Image(IntImages, IMAGE_OBSOLETE_HIDE_HI)));
		obsoleteButton->setTip(false, _("Hiding Obsolete Tech"));
		obsoleteButton->setImages(true, MultipleChoiceButton::Images(Image(IntImages, IMAGE_OBSOLETE_SHOW_UP),
		                                                             Image(IntImages, IMAGE_OBSOLETE_SHOW_UP),
		                                                             Image(IntImages, IMAGE_OBSOLETE_SHOW_HI)));
		obsoleteButton->setTip(true, _("Showing Obsolete Tech"));
		obsoleteButton->move(4 + Image(IntImages, IMAGE_FDP_UP).width() + 4, STAT_SLDY);

		auto weakController = std::weak_ptr<BuildController>(controller);
		obsoleteButton->addOnClickHandler([weakController](W_BUTTON& button)
		{
			if (auto buildController = weakController.lock())
			{
				auto& _obsoleteButton = static_cast<MultipleChoiceButton&>(button);
				auto newValue = !_obsoleteButton.getChoice();
				buildController->setShouldShowRedundantDesign(newValue);
				_obsoleteButton.setChoice(newValue);
			}
		});
	}

	void addFavoriteButton()
	{
		attach(favoriteButton = std::make_shared<MultipleChoiceButton>());
		favoriteButton->style |= WBUT_SECONDARY;
		favoriteButton->setChoice(controller->shouldShowFavorites());
		favoriteButton->setImages(false, MultipleChoiceButton::Images(Image(IntImages, IMAGE_ALLY_RESEARCH),
		                                                              Image(IntImages, IMAGE_ALLY_RESEARCH),
		                                                              Image(IntImages, IMAGE_ALLY_RESEARCH)));
		favoriteButton->setTip(false, _("Showing All Tech\nRight-click to add to Favorites"));
		favoriteButton->setImages(true, MultipleChoiceButton::Images(Image(IntImages, IMAGE_ALLY_RESEARCH_TC),
		                                                             Image(IntImages, IMAGE_ALLY_RESEARCH_TC),
		                                                             Image(IntImages, IMAGE_ALLY_RESEARCH_TC)));
		favoriteButton->setTip(true, _("Showing Only Favorite Tech\nRight-click to remove from Favorites"));
		favoriteButton->move(4 * 2 + Image(IntImages, IMAGE_FDP_UP).width() * 2 + 4 * 2, STAT_SLDY);

		auto weakController = std::weak_ptr<BuildController>(controller);
		favoriteButton->addOnClickHandler([weakController](W_BUTTON& button)
		{
			if (auto buildController = weakController.lock())
			{
				auto& _favoriteButton = static_cast<MultipleChoiceButton&>(button);
				auto newValue = !_favoriteButton.getChoice();
				buildController->setShouldShowFavorite(newValue);
				_favoriteButton.setChoice(newValue);
			}
		});
	}

	std::shared_ptr<BuildController> controller;
	std::shared_ptr<MultipleChoiceButton> obsoleteButton;
	std::shared_ptr<MultipleChoiceButton> favoriteButton;
};

bool BuildController::showInterface()
{
	updateData();
	if (builders.empty())
	{
		return false;
	}

	auto objectsForm = BuildObjectsForm::make(shared_from_this());
	psWScreen->psForm->attach(objectsForm);
	displayStatsForm();
	triggerEvent(TRIGGER_MENU_BUILD_UP);
	return true;
}

std::shared_ptr<StatsForm> BuildController::makeStatsForm()
{
	return BuildStatsForm::make(shared_from_this());
}
