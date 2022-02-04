#include "lib/widget/button.h"
#include "lib/widget/label.h"
#include "lib/widget/bar.h"
#include "lib/sound/audio_id.h"
#include "lib/sound/audio.h"
#include "manufacture.h"

#include <utility>
#include "../mission.h"
#include "../qtscript.h"
#include "src/template.h"

Structure* ManufactureController::highlightedFactory = nullptr;

Factory* getFactoryOrNullptr(Structure* factory)
{
	ASSERT_OR_RETURN(nullptr, StructIsFactory(factory), "Invalid factory pointer");
	return (Factory*)factory->pFunctionality;
}

static uint8_t getProductionLoops(Structure* structure)
{
	if (structure == nullptr)
	{
		return 0;
	}

	auto psFactory = getFactoryOrNullptr(structure);
	ASSERT_NOT_NULLPTR_OR_RETURN(0, psFactory);
	return psFactory->productionLoops;
}

static std::shared_ptr<W_LABEL> makeProductionRunSizeLabel()
{
	auto init = W_LABINIT();
	init.x = OBJ_TEXTX;
	init.y = OBJ_T1TEXTY;
	init.width = 16;
	init.height = 16;
	auto label = std::make_shared<W_LABEL>(&init);
	label->setTransparentToMouse(true);
	return label;
}

void ManufactureController::updateData()
{
	updateFactoriesList();
	updateHighlighted();
	updateManufactureOptionsList();
}

void ManufactureController::adjustFactoryProduction(DroidTemplate* manufactureOption, bool add) const
{
	factoryProdAdjust(getHighlightedObject(), manufactureOption, add);
}

void ManufactureController::adjustFactoryLoop(bool add) const
{
	factoryLoopAdjust(getHighlightedObject(), add);
}

void ManufactureController::releaseFactoryProduction(Structure* structure)
{
	releaseProduction(structure, ModeQueue);
}

void ManufactureController::cancelFactoryProduction(Structure* structure)
{
	if (!StructureIsManufacturingPending(structure))
	{
		return;
	}

	if (!StructureIsOnHoldPending(structure))
	{
		holdProduction(structure, ModeQueue);
		return;
	}

	cancelProduction(structure, ModeQueue);
	audio_PlayTrack(ID_SOUND_WINDOWCLOSE);
}

void ManufactureController::startDeliveryPointPosition() const
{
	auto factory = getHighlightedObject();
	ASSERT_NOT_NULLPTR_OR_RETURN(, factory);

	// make sure that the factory isn't assigned to a commander
	assignFactoryCommandDroid(factory, nullptr);
	auto psFlag = FindFactoryDelivery(factory);
	if (psFlag)
	{
		startDeliveryPosition(psFlag);
	}
}

static inline bool compareFactories(Structure* a, Structure* b)
{
	if (a == nullptr || b == nullptr)
	{
		return (a == nullptr) < (b == nullptr);
	}
	auto x = getFactoryOrNullptr(a);
	ASSERT_NOT_NULLPTR_OR_RETURN(false, x);
	auto y = getFactoryOrNullptr(b);
	ASSERT_NOT_NULLPTR_OR_RETURN(false, y);
	ASSERT_NOT_NULLPTR_OR_RETURN(false, x->getAssemblyPoint());
	ASSERT_NOT_NULLPTR_OR_RETURN(false, y->getAssemblyPoint());
	if (x->getAssemblyPoint()->factoryType != y->getAssemblyPoint()->factoryType)
	{
		return x->getAssemblyPoint()->factoryType < y->getAssemblyPoint()->factoryType;
	}

	return x->getAssemblyPoint()->factoryInc < y->getAssemblyPoint()->factoryInc;
}

void ManufactureController::updateFactoriesList()
{
	factories.clear();

	for (auto& structure : *interfaceStructList())
	{
		if (structure.getState() == STRUCTURE_STATE::BUILT &&
        structure.damageManager->getTimeOfDeath() == 0 &&
        StructIsFactory(structure)) {
			factories.push_back(structure);
		}
	}

	std::sort(factories.begin(), factories.end(), compareFactories);
}

void ManufactureController::updateManufactureOptionsList()
{
	if (auto factory = getHighlightedObject()) {
		stats = fillTemplateList(factory);
	}
	else {
		stats.clear();
	}
}

DroidTemplate* ManufactureController::getObjectStatsAt(size_t objectIndex) const
{
	auto factory = getFactoryOrNullptr(getObjectAt(objectIndex));
	return factory == nullptr ? nullptr : factory->psSubject;
}

void ManufactureController::refresh()
{
	updateData();

	if (objectsSize() == 0) {
		closeInterface();
	}
}

void ManufactureController::clearData()
{
	factories.clear();
	setHighlightedObject(nullptr);
	stats.clear();
}

void ManufactureController::setHighlightedObject(BaseObject* object)
{
	if (object == nullptr)
	{
		highlightedFactory = nullptr;
		return;
	}

	auto factory = dynamic_cast<Structure*>(object);
	ASSERT_OR_RETURN(, StructIsFactory(factory), "Invalid factory pointer");
	highlightedFactory = factory;
}

class ManufactureObjectButton : public ObjectButton
{
private:
	typedef ObjectButton BaseWidget;

protected:
	ManufactureObjectButton()
	= default;

public:
	static std::shared_ptr<ManufactureObjectButton> make(const std::shared_ptr<ManufactureController>& controller,
	                                                     size_t objectIndex)
	{
		class make_shared_enabler : public ManufactureObjectButton
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->objectIndex = objectIndex;
		widget->initialize();
		return widget;
	}

	void jump() override
	{
		if (!offWorldKeepLists)
		{
			BaseWidget::jump();
		}
	}

	void clickPrimary() override
	{
		controller->clearStructureSelection();
		controller->selectObject(controller->getObjectAt(objectIndex));
		jump();
		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

protected:
	void initialize()
	{
		attach(factoryNumberLabel = std::make_shared<W_LABEL>());
		factoryNumberLabel->setGeometry(OBJ_TEXTX, OBJ_B1TEXTY, 16, 16);
	}

	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto factory = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, factory);
		if (isDead(factory))
		{
			ASSERT_FAILURE(!isDead(factory), "!isDead(factory)", AT_MACRO, __FUNCTION__, "Factory is dead");
			// ensure the backing information is refreshed before the next draw
			intRefreshScreen();
			return;
		}
		displayIMD(Image(), ImdObject::Structure(factory), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();
		auto factory = getFactoryOrNullptr(controller->getObjectAt(objectIndex));
		ASSERT_NOT_NULLPTR_OR_RETURN(, factory);
		if (factory->getAssemblyPoint() == nullptr) {
			factoryNumberLabel->setString("");
			return;
		}
		factoryNumberLabel->setString(WzString::fromUtf8(astringf("%u", factory->getAssemblyPoint()->factoryInc + 1)));
	}

	std::string getTip() override
	{
		auto factory = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN("", factory);
		return getStatsName(factory->getStats());
	}

	ManufactureController& getController() const override
	{
		return *controller;
	}

private:
	std::shared_ptr<ManufactureController> controller;
	std::shared_ptr<W_LABEL> factoryNumberLabel;
};

class ManufactureStatsButton : public StatsButton
{
private:
	typedef StatsButton BaseWidget;

protected:
	ManufactureStatsButton()
	= default;

public:
	static std::shared_ptr<ManufactureStatsButton> make(const std::shared_ptr<ManufactureController>& controller,
	                                                    size_t objectIndex)
	{
		class make_shared_enabler : public ManufactureStatsButton
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

		auto factory = controller->getObjectAt(objectIndex);
		auto production = getStats();
		auto productionPending = factory && StructureIsManufacturingPending(factory);
		auto objectImage = productionPending && production
			                   ? ImdObject::DroidTemplate(production)
			                   : ImdObject::Component(nullptr);

		displayIMD(Image(), objectImage, xOffset, yOffset);

		if (productionPending && StructureIsOnHoldPending(factory))
		{
			iV_DrawImage(IntImages, ((realTime / 250) % 2) == 0 ? IMAGE_BUT0_DOWN : IMAGE_BUT_HILITE, xOffset + x(),
			             yOffset + y());
		}
		else
		{
			displayIfHighlight(xOffset, yOffset);
		}
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();
		auto factory = controller->getObjectAt(objectIndex);
		updateProgressBar(factory);
		updateProductionRunSizeLabel(factory, FactoryGetTemplate(StructureGetFactory(factory)));
	}

private:
	DroidTemplate* getStats() override
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
		attach(productionRunSizeLabel = makeProductionRunSizeLabel());
		productionRunSizeLabel->setFontColour(WZCOL_ACTION_PRODUCTION_RUN_TEXT);
	}

	void updateProductionRunSizeLabel(Structure* factory, DroidTemplate* droidTemplate)
	{
		auto productionRemaining = getProduction(factory, droidTemplate).numRemaining();
		if (productionRemaining > 0 && factory && StructureIsManufacturingPending(factory))
		{
			productionRunSizeLabel->setString(WzString::fromUtf8(astringf("%d", productionRemaining)));
			productionRunSizeLabel->show();
		}
		else
		{
			productionRunSizeLabel->hide();
		}
	}

	void updateProgressBar(Structure* factory)
	{
		progressBar->hide();

		ASSERT_NOT_NULLPTR_OR_RETURN(, factory);
		ASSERT_OR_RETURN(, !factory->damageManager->isDead(), "Factory is dead");

		if (!StructureIsManufacturingPending(factory)) {
			return;
		}

		auto manufacture = StructureGetFactory(factory);
		ASSERT_NOT_NULLPTR_OR_RETURN(, manufacture);

		if (manufacture->psSubject != nullptr && manufacture->buildPointsRemaining < calcTemplateBuild(
			manufacture->psSubject)) {
			// Started production. Set the colour of the bar to yellow.
			int buildPointsTotal = calcTemplateBuild(FactoryGetTemplate(manufacture));
			int buildRate = manufacture->timeStartHold == 0 ? getBuildingProductionPoints(factory) : 0;
			formatTime(progressBar.get(), buildPointsTotal - manufacture->buildPointsRemaining, buildPointsTotal,
			           buildRate, _("Construction Progress"));
		}
		else
		{
			// Not yet started production.
			int neededPower = checkPowerRequest(factory);
			int powerToBuild = manufacture->psSubject ? calcTemplatePower(manufacture->psSubject) : 0;
			formatPower(progressBar.get(), neededPower, powerToBuild);
		}
	}

	bool isHighlighted() const override
	{
		auto factory = controller->getObjectAt(objectIndex);
		return factory && (factory->selected || factory == controller->getHighlightedObject());
	}

	void clickPrimary() override
	{
		auto factory = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, factory);
		controller->releaseFactoryProduction(factory);
		controller->clearStructureSelection();
		controller->selectObject(factory);
		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

	void clickSecondary() override
	{
		auto factory = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, factory);
		controller->clearStructureSelection();
		controller->cancelFactoryProduction(factory);
		controller->setHighlightedObject(factory);
		controller->refresh();
		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

	std::shared_ptr<ManufactureController> controller;
	std::shared_ptr<W_LABEL> productionRunSizeLabel;
	size_t objectIndex;
};

class ManufactureOptionButton : public StatsFormButton
{
private:
	typedef StatsFormButton BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<ManufactureOptionButton> make(const std::shared_ptr<ManufactureController>& controller,
	                                                     size_t manufactureOptionIndex)
	{
		class make_shared_enabler : public ManufactureOptionButton
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->manufactureOptionIndex = manufactureOptionIndex;
		widget->initialize();
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto stat = getStats();
		ASSERT_NOT_NULLPTR_OR_RETURN(, stat);

		displayIMD(Image(), ImdObject::DroidTemplate(stat), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

private:
	DroidTemplate* getStats() override
	{
		return controller->getStatsAt(manufactureOptionIndex);
	}

	void initialize()
	{
		addCostBar();
		addProductionRunSizeLabel();
	}

	void addProductionRunSizeLabel()
	{
		attach(productionRunSizeLabel = makeProductionRunSizeLabel());
	}

	void updateProductionRunSizeLabel(Structure* structure, DroidTemplate* droidTemplate)
	{
		auto production = getProduction(structure, droidTemplate);
		if (production.isValid())
		{
			auto productionLoops = getProductionLoops(structure);
			auto labelText = astringf(productionLoops > 0 ? "%d/%d" : "%d", production.numRemaining(),
			                          production.quantity);
			productionRunSizeLabel->setString(WzString::fromUtf8(labelText));
			productionRunSizeLabel->show();
		}
		else
		{
			productionRunSizeLabel->hide();
		}
	}

	bool isHighlighted() const override
	{
		return controller->isHighlightedObjectStats(manufactureOptionIndex);
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();
		if (isMouseOverWidget())
		{
			intSetShadowPower(getCost());
		}
		costBar->majorSize = std::min(100, (int32_t)(getCost() / POWERPOINTS_DROIDDIV));
		updateProductionRunSizeLabel(controller->getHighlightedObject(), getStats());
	}

	uint32_t getCost() override
	{
		DroidTemplate* psTemplate = getStats();
		return psTemplate ? calcTemplatePower(psTemplate) : 0;
	}

	void clickPrimary() override
	{
		adjustFactoryProduction(true);
	}

	void clickSecondary() override
	{
		adjustFactoryProduction(false);
	}

	void adjustFactoryProduction(bool add)
	{
		auto clickedStats = controller->getStatsAt(manufactureOptionIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, clickedStats);

		auto controllerRef = controller;
		widgScheduleTask([add, clickedStats, controllerRef]()
		{
			controllerRef->adjustFactoryProduction(clickedStats, add);
		});
	}

	std::shared_ptr<ManufactureController> controller;
	std::shared_ptr<W_LABEL> productionRunSizeLabel;
	size_t manufactureOptionIndex;
};

class ManufactureObjectsForm : public ObjectsForm
{
private:
	typedef ObjectsForm BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<ManufactureObjectsForm> make(const std::shared_ptr<ManufactureController>& controller)
	{
		class make_shared_enabler : public ManufactureObjectsForm
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->initialize();
		return widget;
	}

	std::shared_ptr<StatsButton> makeStatsButton(size_t buttonIndex) const override
	{
		return ManufactureStatsButton::make(controller, buttonIndex);
	}

	std::shared_ptr<ObjectButton> makeObjectButton(size_t buttonIndex) const override
	{
		return ManufactureObjectButton::make(controller, buttonIndex);
	}

protected:
	ManufactureController& getController() const override
	{
		return *controller;
	}

private:
	std::shared_ptr<ManufactureController> controller;
};

class ManufactureStatsForm : public ObjectStatsForm
{
private:
	typedef ObjectStatsForm BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<ManufactureStatsForm> make(const std::shared_ptr<ManufactureController>& controller)
	{
		class make_shared_enabler : public ManufactureStatsForm
		{
		};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->initialize();
		return widget;
	}

	std::shared_ptr<StatsFormButton> makeOptionButton(size_t buttonIndex) const override
	{
		return ManufactureOptionButton::make(controller, buttonIndex);
	}

protected:
	ManufactureController& getController() const override
	{
		return *controller;
	}

private:
	void initialize() override
	{
		BaseWidget::initialize();
		addObsoleteButton();
		attach(std::make_shared<DeliveryPointButton>(controller));
		addLoopProductionWidgets();
	}

	void addObsoleteButton()
	{
		auto obsoleteButton = std::make_shared<MultipleChoiceButton>();
		attach(obsoleteButton);
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

		auto weakController = std::weak_ptr<ManufactureController>(controller);
		obsoleteButton->addOnClickHandler([weakController](W_BUTTON& button)
		{
			if (auto manufactureController = weakController.lock())
			{
				auto& obsoleteButton = dynamic_cast<MultipleChoiceButton&>(button);
				auto newValue = !obsoleteButton.getChoice();
				manufactureController->setShouldShowRedundantDesign(newValue);
				obsoleteButton.setChoice(newValue);
			}
		});
	}

	void addLoopProductionWidgets()
	{
		auto loopProductionButton = std::make_shared<LoopProductionButton>(controller);
		attach(loopProductionButton);
		loopProductionButton->move(STAT_SLDX + STAT_SLDWIDTH + 2, STAT_SLDY);

		auto loopProductionLabel = std::make_shared<LoopProductionLabel>(controller);
		attach(loopProductionLabel);
		loopProductionLabel->setGeometry(loopProductionButton->x() - 15, loopProductionButton->y(), 12, 15);
	}

	std::shared_ptr<ManufactureController> controller;

	class DeliveryPointButton : public W_BUTTON
	{
	private:
		typedef W_BUTTON BaseWidget;

	public:
		explicit DeliveryPointButton(const std::shared_ptr<ManufactureController>& controller): BaseWidget(),
			controller(controller)
		{
			style |= WBUT_SECONDARY;
			move(4, STAT_SLDY);
			setTip(_("Factory Delivery Point"));

			auto weakController = std::weak_ptr<ManufactureController>(controller);
			addOnClickHandler([weakController](W_BUTTON&)
			{
				auto controller = weakController.lock();
				ASSERT_NOT_NULLPTR_OR_RETURN(, controller);
				controller->startDeliveryPointPosition();
			});
		}

	protected:
		void display(int xOffset, int yOffset) override
		{
			updateLayout();
			BaseWidget::display(xOffset, yOffset);
		}

	private:
		void updateLayout()
		{
			auto factory = controller->getHighlightedObject();
			ASSERT_NOT_NULLPTR_OR_RETURN(, factory);

			switch (factory->getStats()->type)
			{
			default:
      case STRUCTURE_TYPE::FACTORY:
				setImages(Image(IntImages, IMAGE_FDP_UP), Image(IntImages, IMAGE_FDP_DOWN),
				          Image(IntImages, IMAGE_FDP_HI));
				break;
			case STRUCTURE_TYPE::CYBORG_FACTORY:
				setImages(Image(IntImages, IMAGE_CDP_UP), Image(IntImages, IMAGE_CDP_DOWN),
				          Image(IntImages, IMAGE_CDP_HI));
				break;
			case STRUCTURE_TYPE::VTOL_FACTORY:
				setImages(Image(IntImages, IMAGE_VDP_UP), Image(IntImages, IMAGE_VDP_DOWN),
				          Image(IntImages, IMAGE_VDP_HI));
				break;
			}
		}

		std::shared_ptr<ManufactureController> controller;
	};

	class LoopProductionButton : public W_BUTTON
	{
	private:
		typedef W_BUTTON BaseWidget;

	public:
		explicit LoopProductionButton(std::shared_ptr<ManufactureController>  controller): BaseWidget(),
			controller(std::move(controller))
		{
			style |= WBUT_SECONDARY;
			setImages(Image(IntImages, IMAGE_LOOP_UP), Image(IntImages, IMAGE_LOOP_DOWN),
			          Image(IntImages, IMAGE_LOOP_HI));
			setTip(_("Loop Production"));
		}

		void released(W_CONTEXT* psContext, WIDGET_KEY key) override
		{
			BaseWidget::released(psContext, key);
			controller->adjustFactoryLoop(key == WKEY_PRIMARY);
		}

	protected:
		void display(int xOffset, int yOffset) override
		{
			updateLayout();
			BaseWidget::display(xOffset, yOffset);
		}

		void updateLayout()
		{
			setState(getProductionLoops(controller->getHighlightedObject()) == 0 ? 0 : WBUT_CLICKLOCK);
		}

	private:
		std::shared_ptr<ManufactureController> controller;
	};

	class LoopProductionLabel : public W_LABEL
	{
	private:
		typedef W_LABEL BaseWidget;

	public:
		explicit LoopProductionLabel(std::shared_ptr<ManufactureController>  controller): BaseWidget(),
			controller(std::move(controller))
		{
		}

	protected:
		void display(int xOffset, int yOffset) override
		{
			updateLayout();
			BaseWidget::display(xOffset, yOffset);
		}

		void updateLayout()
		{
			auto productionLoops = getProductionLoops(controller->getHighlightedObject());
			if (productionLoops != lastProductionLoop)
			{
				lastProductionLoop = productionLoops;
				setString(WzString::fromUtf8(getNewString()));
			}
		}

	private:
		std::string getNewString() const
		{
			switch (lastProductionLoop)
			{
			case 0:
				return "";
			case INFINITE_PRODUCTION:
				return "∞";
			default:
				return astringf("%u", lastProductionLoop + 1);
			}
		}

		uint8_t lastProductionLoop = 0;
		std::shared_ptr<ManufactureController> controller;
	};
};

bool ManufactureController::showInterface()
{
	updateData();
	if (factories.empty())
	{
		return false;
	}

	auto objectsForm = ManufactureObjectsForm::make(shared_from_this());
	psWScreen->psForm->attach(objectsForm);
	displayStatsForm();
	triggerEvent(TRIGGER_MENU_MANUFACTURE_UP);
	return true;
}

std::shared_ptr<StatsForm> ManufactureController::makeStatsForm()
{
	return ManufactureStatsForm::make(shared_from_this());
}
