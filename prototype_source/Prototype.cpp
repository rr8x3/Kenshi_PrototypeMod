#include <Debug.h>

#include <sstream>

#include <kenshi/RaceData.h>
#include <kenshi/gui/TitleScreen.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Damages.h>
#include <kenshi/Enums.h>

#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_Delegate.h>

#include <kenshi/StateBroadcastData.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/Faction.h>
#include <kenshi/Gear.h>
#include <kenshi/CharMovement.h>
#include <kenshi/PhysicsActual.h>
#include <kenshi/util/UtilityT.h>
#include <kenshi/AI/AITaskSystem.h>
#include <ogre/OgreEntity.h>
#include <ogre/OgreSceneNode.h>
#include <ogre/Math/Simple/OgreAabb.h>
#include <kenshi/util/lektor.h>
#include <kenshi/util/OgreUnordered.h>
#include <kenshi/InputHandler.h>
#include <kenshi/CharStats.h>
#include <kenshi/LocaleInfo.h>
#include <kenshi/gui/OptionsWindow.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
#include <kenshi/gui/InventoryGUI.h>
#include <mygui/common/baselayout/BaseLayout.h>
#include <core/Functions.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/Dialogue.h>
#include <kenshi/util/StringPair.h>
#include <kenshi/Animation/AnimationClass.h>
#include <kenshi/util/TimeOfDay.h>
#include <cmath>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// On save/load, serialise_hook/loadFromSerialise_hook get the ID from their GameData
// That ID is eventually passed on to here, where it is used to find or create a .ini file assigned to it

// After looking into it, I think protoRace's GameData could be written directly into the save using updateFrom() then loaded using updateData()
// I'm not sure how exactly to do it though, and this works fine. Would be way cleaner and theoretically work with all RaceData values, however.
// UPDATE: I tried and managed to save protoRace's data and load it (I think) but couldn't figure out how to update the RaceData from it. I can't really find any documentation but maybe I'm just not smart enough for allat

// Adapted from Disarm's source code
static std::string GetSaveDataPath(const std::string& uid) {
    HMODULE hSelf = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetSaveDataPath),
        &hSelf);
    if (hSelf == NULL) return "";

    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(hSelf, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";

    std::string p(path, len);
    size_t slash = p.find_last_of("\\/");
    if (slash == std::string::npos) return "";

    return p.substr(0, slash + 1) + "SaveData\\" + uid + ".ini";
}




MyGUI::Window* mainWindow;
MyGUI::Window* healthWindow;
static int g_mainListDiff = -1;


//    Extraction globals
AnimationData* medicAnim = NULL;
bool playingExtractAnim = false;
float g_extractReward = 20.0f;
static Character* g_extractTarget;
double now;
Character* extractActor;
bool travelingToTarget = false;
CharMovement* actorMovement;
double travelStartTime = 0.0;
MedicalSystem::HealthPartStatus* extractTargetHead;
float percentHeadDamage = 0.8;
float extractRadius = 12;
MyGUI::ProgressBar* extractBar = NULL;
//______________________________


static ContextMenu* g_activeMenu = NULL;
static std::vector<MyGUI::Widget*> g_customWidgets;

// Can't access ContextMenuGUI unless you do this, thanks to Disarm (https://github.com/dafitime/disarm/blob/master/Disarm.cpp) for explanation: 
 
    // ContextMenu itself is already fully defined by kenshi/PlayerInterface.h
    // (already included above), showContextMenu and all — including the real
    // kenshi/gui/ContextMenu.h as well conflicts (duplicate class definition,
    // two SDK headers disagreeing). ContextMenuGUI, however, is only forward-
    // declared there, so it still needs this layout-compatible stub
class ContextMenuGUI : public wraps::BaseLayout, public Ogre::GeneralAllocatedObject
{
public:
    hand contextMenuTarget;
    std::string name;
    void* nameText;
    MyGUI::Widget* optionsList;
    MyGUI::types::TCoord<int> optionCoords;
    MyGUI::types::TCoord<int> buttonCoords;
    MyGUI::types::TCoord<int> valueCoords;
};

// Core globals
RaceData* protoRace = RaceData::getRaceData("10-custom_skeletons.mod"); // StringIDs for lookup are in the FCS
GameData* protoData = protoRace->data;
RaceData* skeletonRace = RaceData::getRaceData("17946-stick_people.mod");

float currentPoints = 40.0f;
MyGUI::TextBox* pointsDisplay;
float refundMult = 0.5f;
//_____________________________

// Context menu necessities
struct StretchedWidget {
    MyGUI::Widget* widget;
    int originalHeight;
    int targetHeight;
};
static std::vector<StretchedWidget> g_stretchedParents;
static bool g_extractRowActive = false;

static void CleanupCustomUI()
{
    g_extractRowActive = false;
    for (size_t i = 0; i < g_stretchedParents.size(); ++i) {
        MyGUI::Widget* w = g_stretchedParents[i].widget;
        if (w != NULL) {
            MyGUI::IntSize sz = w->getSize();
            w->setSize(MyGUI::IntSize(sz.width, g_stretchedParents[i].originalHeight));
        }
    }
    g_stretchedParents.clear();
    MyGUI::Gui* mygui = MyGUI::Gui::getInstancePtr();
    for (size_t i = 0; i < g_customWidgets.size(); ++i) {
        if (g_customWidgets[i] != NULL)
            mygui->destroyWidget(g_customWidgets[i]);
    }
    g_customWidgets.clear();
}
//______________________________________


typedef float RaceData::*FieldPtr;
typedef bool RaceData::*BoolPtr;


struct StatEntry {
    bool isStatMod;
    std::string key;
    std::string label;
    float min, max, step;
    float price;
    float refundMult;
    float baseline;
    bool reversed;
    float currentValue;
    MyGUI::TextBox* valueDisplay;
    std::vector<StatsEnumerated> enums;
    FieldPtr fieldTarget;
};

struct BoolEntry {
    std::string key;
    std::string label;
    float price;
    bool baseline;
    bool reversed;
    bool currentValue;
    BoolPtr boolTarget;
};

struct HealthEntry {
    std::string key;
    std::string label;
    float min, max, step;
    float price;
    float refundMult;
    float currentValue;
    MyGUI::TextBox* valueDisplay;
    MedicalSystem::HealthPartStatus* part;

};


std::vector<StatEntry> statList;
std::vector<BoolEntry> boolList;
std::vector<HealthEntry> healthList;

StatEntry makeStatEntry(std::string key, std::string label, float min, float max, float step, float baseline, float price, FieldPtr fieldTarget) {
    StatEntry entry;
    entry.isStatMod = false;
    entry.key = key;
    entry.label = label;
    entry.min = min;
    entry.max = max;
    entry.step = step;
    entry.baseline = baseline;
    entry.reversed = false;
    entry.price = price;
    entry.refundMult = 0.5f;
    entry.currentValue = 0.0f;
    entry.valueDisplay = NULL;
    entry.fieldTarget = fieldTarget;

    return entry;
}

BoolEntry makeBoolEntry(std::string key, std::string label, float price, bool baseline, BoolPtr boolTarget) {
    BoolEntry entry;
    entry.key = key;
    entry.label = label;
    entry.price = price;
    entry.baseline = baseline;
    entry.reversed = false;
    entry.currentValue = entry.baseline;
    entry.boolTarget = boolTarget;

    return entry;
}

StatEntry makeStatModEntry(std::string key, std::string label, float min, float max, float step, float price, std::vector<StatsEnumerated> enums) {
    StatEntry entry;
    entry.isStatMod = true;
    entry.key = key;
    entry.label = label;
    entry.min = min;
    entry.max = max;
    entry.step = step;
    entry.baseline = 1;
    entry.price = price;
    entry.refundMult = 0.5f;
    entry.currentValue = 1;
    entry.enums = enums;

    return entry;
}

StatEntry makeSingleStatModEntry(std::string key, std::string label, float min, float max, float step, float price, StatsEnumerated enumIndex) {
    std::vector<StatsEnumerated> singleModVec;
    singleModVec.push_back(enumIndex);
    
    StatEntry entry = makeStatModEntry(key, label, min, max, step, price, singleModVec);

    return entry;
}

HealthEntry makeHealthPartEntry(std::string key, std::string label, float min, float max, float step, float price, MedicalSystem::HealthPartStatus* part) {
    HealthEntry entry;
    entry.key = key;
    entry.label = label;
    entry.min = min;
    entry.max = max;
    entry.step = step;
    entry.price = price;
    entry.refundMult = 1.0f;
    entry.currentValue = part->_maxHealth;
    entry.valueDisplay = NULL;
    entry.part = part;


    return entry;
}

void createStatList() {
    //TEMPLATE: statList.push_back(makeStatEntry("", "", -70.0f, 200.0f, 10.0f, protoRace->, 0.0f, &RaceData::));

    //statList.push_back(makeStatEntry("runSpeedMinSkill", "Starting Speed", -70.0f, 200.0f, 10.0f, protoRace->runSpeedMinSkill, 20.0f, &RaceData::runSpeedMinSkill)); // Not sure if I should keep this
    statList.push_back(makeStatEntry("runSpeedMaxSkill", "Mastered Speed", -70.0f, 200.0f, 10.0f, protoRace->runSpeedMaxSkill, 15.0f, &RaceData::runSpeedMaxSkill));
    //statList.push_back(makeStatEntry("originalBloodMin", "Starting Blood", -70.0f, 200.0f, 10.0f, protoRace->originalBloodMin, 20.0f, &RaceData::originalBloodMin)); // Not sure if I should keep this
    statList.push_back(makeStatEntry("originalBloodMax", "Max Potential Blood", -70.0f, 200.0f, 10.0f, protoRace->originalBloodMax, 10.0f, &RaceData::originalBloodMax));
    statList.push_back(makeStatEntry("healRate", "Heal Rate", -70.0f, 200.0f, 5.0f, protoRace->healRate, 20.0f, &RaceData::healRate)); // Base in FCS is 1.0, might be too harsh. Costs 400 points to get to normal skeleton base
    //                                                                                                                                    Maybe set to 15 price? or 10 step 30 price
    StatEntry bleedRate = makeStatEntry("bleedRate", "Bleed Rate Reduction", -200.0f, 95.0f, 5.0f, protoRace->bleedRate, 10.0f, &RaceData::bleedRate); // Base in FCS is 1.0
    bleedRate.reversed = true;
    statList.push_back(bleedRate);
    statList.push_back(makeStatEntry("swimSpeed", "Swim Speed", -80.0f, 200.0f, 20.0f, protoRace->swimSpeed, 10.0f, &RaceData::swimSpeed)); // make cheap, might remove
    statList.push_back(makeStatEntry("visionMultiplier", "Vision Multiplier", -80.0f, 200.0f, 20.0f, protoRace->visionMultiplier, 10.0f, &RaceData::visionMultiplier));
}

void createBoolList() {

    BoolEntry noHats = makeBoolEntry("noHats", "Can Wear Hats", 40.0f, false, &RaceData::noHats);
    noHats.reversed = true;
    boolList.push_back(noHats);
    BoolEntry noShirts = makeBoolEntry("noShirts", "Can Wear Shirts", 20.0f, false, &RaceData::noShirts);
    noShirts.reversed = true;
    boolList.push_back(noShirts);
    BoolEntry noShoes = makeBoolEntry("noShoes", "Can Wear Shoes", 30.0f, false, &RaceData::noShoes);
    noShoes.reversed = true;
    boolList.push_back(noShoes);
}

void createHealthList(MedicalSystem* charMed) {
    if (!healthList.empty()) { healthList.clear(); }
    // Kenshi has a bug where chest and stomach randomly switch, so they have to be found manually. KEP might fix it but adds a dependency
    int chestIndex = charMed->getPart(1)->hitChance >= 120 ? 1 : 2; // Chest hit chance is 140, using >= 120 in case changes for some reason
    int stomachIndex = chestIndex == 1 ? 2 : 1; // Stomach is either 1 or 2 like chest, so it has to be what chest isn't

    healthList.push_back(makeHealthPartEntry("head", "Head", 50, 300, 10, 20, charMed->getPart(0)));
    healthList.push_back(makeHealthPartEntry("chest", "Chest", 50, 300, 10, 20, charMed->getPart(chestIndex)));
    healthList.push_back(makeHealthPartEntry("stomach", "Stomach", 50, 300, 10, 20, charMed->getPart(stomachIndex)));
    healthList.push_back(makeHealthPartEntry("leftArm", "Left Arm", 50, 300, 10, 10, charMed->getPart(3)));
    healthList.push_back(makeHealthPartEntry("rightArm", "Right Arm", 50, 300, 10, 10, charMed->getPart(4)));
    healthList.push_back(makeHealthPartEntry("leftLeg", "Left Leg", 50, 300, 10, 10, charMed->getPart(5)));
    healthList.push_back(makeHealthPartEntry("rightLeg", "Right Leg", 50, 300, 10, 10, charMed->getPart(6)));
}


std::vector<StatEntry> attributeStats;
std::vector<StatEntry> athleticStats;
std::vector<StatEntry> weaponStats;
std::vector<StatEntry> combatStats;
std::vector<StatEntry> rangedStats;
std::vector<StatEntry> precisionStats;
std::vector<StatEntry> stealthStats;
std::vector<StatEntry> scienceStats;
std::vector<StatEntry> smithingStats;
std::vector<StatEntry> laborStats;

void createStatModList() {

    // --- WEAPONS ---

    std::vector<StatsEnumerated> weaponEnums;
    weaponEnums.push_back(StatsEnumerated::STAT_KATANAS);
    weaponEnums.push_back(StatsEnumerated::STAT_SABRES);
    weaponEnums.push_back(StatsEnumerated::STAT_HACKERS);
    weaponEnums.push_back(StatsEnumerated::STAT_HEAVYWEAPONS);
    weaponEnums.push_back(StatsEnumerated::STAT_BLUNT);
    weaponEnums.push_back(StatsEnumerated::STAT_POLEARMS);
    weaponStats.push_back(makeStatModEntry("weapons", "Weapon XP", 0.3f, 3.0f, 0.1f, 20.0f, weaponEnums));

    // --- ATHLETICS ---
    
    std::vector<StatsEnumerated> athleticsEnums;
    athleticsEnums.push_back(StatsEnumerated::STAT_ATHLETICS);
    athleticsEnums.push_back(StatsEnumerated::STAT_SWIMMING);
    athleticStats.push_back(makeStatModEntry("athletics", "Athletics", 0.3f, 3.0f, 0.1f, 20.0f, athleticsEnums));

    // --- ATTACK ---
    
    std::vector<StatsEnumerated> attackEnums;
    attackEnums.push_back(StatsEnumerated::STAT_MELEE_ATTACK);
    attackEnums.push_back(StatsEnumerated::STAT_MARTIALARTS);
    combatStats.push_back(makeStatModEntry("attack", "Attack", 0.3f, 3.0f, 0.1f, 20.0f, attackEnums));

    // --- DEFENSE ---
    std::vector<StatsEnumerated> defenseEnums;
    defenseEnums.push_back(StatsEnumerated::STAT_MELEE_DEFENCE);
    defenseEnums.push_back(StatsEnumerated::STAT_DODGE);
    combatStats.push_back(makeStatModEntry("defense", "Defense", 0.3f, 3.0f, 0.1f, 20.0f, defenseEnums));

    // --- RANGED ---
    
    std::vector<StatsEnumerated> rangedEnums;
    rangedEnums.push_back(StatsEnumerated::STAT_CROSSBOWS);
    rangedEnums.push_back(StatsEnumerated::STAT_TURRETS);
    rangedStats.push_back(makeStatModEntry("ranged", "Ranged", 0.3f, 3.0f, 0.1f, 20.0f, rangedEnums));

    // --- PRECISION (FRIENDLY_FIRE + PERCEPTION) ---
    
    std::vector<StatsEnumerated> precisionEnums;
    precisionEnums.push_back(StatsEnumerated::STAT_FRIENDLY_FIRE);
    precisionEnums.push_back(StatsEnumerated::STAT_PERCEPTION);
    precisionStats.push_back(makeStatModEntry("precision", "Precision Shooting", 0.3f, 3.0f, 0.1f, 15.0f, precisionEnums));

    // --- SMITHING ---
    
    std::vector<StatsEnumerated> smithingEnums;
    smithingEnums.push_back(StatsEnumerated::STAT_SMITHING_ARMOUR);
    smithingEnums.push_back(StatsEnumerated::STAT_SMITHING_WEAPON);
    smithingEnums.push_back(StatsEnumerated::STAT_SMITHING_BOW);
    smithingStats.push_back(makeStatModEntry("smithing", "Smithing", 0.3f, 3.0f, 0.1f, 20.0f, smithingEnums));

    // --- LABOR ---
    
    std::vector<StatsEnumerated> laborEnums;
    laborEnums.push_back(StatsEnumerated::STAT_LABOURING);
    laborEnums.push_back(StatsEnumerated::STAT_FARMING);
    laborEnums.push_back(StatsEnumerated::STAT_COOKING);
    laborStats.push_back(makeStatModEntry("labor", "Labor", 0.3f, 3.0f, 0.1f, 20.0f, laborEnums));


    
    
    // --- ATTRIBUTES ---
   
    attributeStats.push_back(makeSingleStatModEntry("strength", "Strength", 0.3f, 3.0f, 0.1f, 30.0f, StatsEnumerated::STAT_STRENGTH));
    attributeStats.push_back(makeSingleStatModEntry("dexterity", "Dexterity", 0.3f, 3.0f, 0.1f, 30.0f, StatsEnumerated::STAT_DEXTERITY));
    attributeStats.push_back(makeSingleStatModEntry("toughness", "Toughness", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_TOUGHNESS));

    // --- THIEVERY ---

    stealthStats.push_back(makeSingleStatModEntry("stealth", "Stealth", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_STEALTH));
    stealthStats.push_back(makeSingleStatModEntry("lockpicking", "Lockpicking", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_LOCKPICKING));
    stealthStats.push_back(makeSingleStatModEntry("thieving", "Thieving", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_THIEVING));
    stealthStats.push_back(makeSingleStatModEntry("assassination", "Assassination", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_ASSASSINATION));

    // --- SCIENCES ---
    
    scienceStats.push_back(makeSingleStatModEntry("medic", "Medic", 0.3f, 3.0f, 0.1f, 10.0f, StatsEnumerated::STAT_MEDIC));
    scienceStats.push_back(makeSingleStatModEntry("engineering", "Engineering", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_ENGINEERING));
    scienceStats.push_back(makeSingleStatModEntry("robotics", "Robotics", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_ROBOTICS));
    scienceStats.push_back(makeSingleStatModEntry("science", "Science", 0.3f, 3.0f, 0.1f, 20.0f, StatsEnumerated::STAT_SCIENCE));
    




}

void applyStatMod(std::vector<StatEntry> &stats) {

    for (size_t i = 0; i < stats.size(); i++) {
       
        std::vector<StatsEnumerated> &statEnums = stats[i].enums;
        std::stringstream debugEnum;
        debugEnum << stats[i].enums.size();
        DebugLog(stats[i].key + "ENUMS: " + debugEnum.str());

        for (size_t e = 0; e < statEnums.size(); e++) {

            protoRace->statMods[statEnums[e]] = stats[i].currentValue;
            std::stringstream debugVal;
            debugVal << stats[i].currentValue;
            DebugLog("APPLIED: " + stats[i].key + ", " + debugVal.str());

        }

    }



}

void refreshPoints()
{
    std::ostringstream pointsText;
    pointsText << currentPoints;
    pointsDisplay->setCaptionWithReplacing(pointsText.str());
}

std::map<MyGUI::Widget*, std::string> widgetToType;
std::map<MyGUI::Widget*, StatEntry*> widgetToStat;
std::map<MyGUI::Widget*, HealthEntry*> widgetToHealth;

void OnMinusPress(MyGUI::WidgetPtr sender) {

    StatEntry* stat = widgetToStat[sender];
    float base = (stat->isStatMod ? 1.0f : 0.0f);

    float oldValue = stat->currentValue;
    float newValue = std::max(stat->min, oldValue - stat->step);
    stat->currentValue = newValue;

    if (oldValue != newValue) {
        float priceMult = (newValue < base ? stat->refundMult : 1.0f);
        currentPoints += stat->price * priceMult;
    }


    std::ostringstream valueText;
    std::string suffix = (stat->isStatMod ? "x" : "%");
    std::string prefix = (stat->currentValue >= 0 && !stat->isStatMod ? "+" : "");
    valueText << prefix << stat->currentValue << suffix;
    stat->valueDisplay->setCaptionWithReplacing(valueText.str());

    refreshPoints();

}

void OnPlusPress(MyGUI::WidgetPtr sender) {

    StatEntry* stat = widgetToStat[sender];

    float base = (stat->isStatMod ? 1.0f : 0.0f);

    float oldValue = stat->currentValue;
    float newValue = std::max(stat->min, oldValue + stat->step);
    stat->currentValue = newValue;

    if (oldValue != newValue) {
        float priceMult = (oldValue < base ? stat->refundMult : 1.0f);
        currentPoints -= stat->price * priceMult;
    }



    std::ostringstream valueText;
    std::string suffix = (stat->isStatMod ? "x" : "%");
    std::string prefix = (stat->currentValue >= 0 && !stat->isStatMod ? "+" : "");
    valueText << prefix << stat->currentValue << suffix;
    stat->valueDisplay->setCaptionWithReplacing(valueText.str());

    refreshPoints();
}

void applyAllStats() {

    for (size_t i = 0; i < statList.size(); i++) {
        if (statList[i].reversed) { protoRace->*(statList[i].fieldTarget) = statList[i].baseline * (1.0f - statList[i].currentValue / 100.0f); }
        else { protoRace->*(statList[i].fieldTarget) = statList[i].baseline * (1.0f + statList[i].currentValue / 100.0f); }
    }

    for (size_t i = 0; i < boolList.size(); i++) {
        if (boolList[i].reversed) { protoRace->*(boolList[i].boolTarget) = !boolList[i].currentValue; }
        else { protoRace->*(boolList[i].boolTarget) = boolList[i].currentValue; }

    }

    applyStatMod(attributeStats);
    applyStatMod(athleticStats);
    applyStatMod(weaponStats);
    applyStatMod(combatStats);
    applyStatMod(rangedStats);
    applyStatMod(stealthStats);
    applyStatMod(scienceStats);
    applyStatMod(smithingStats);
    applyStatMod(laborStats);

    DebugLog("Stats applied");

}

void applyHealth() {
    
    for (size_t i = 0; i < healthList.size(); i++) {
        healthList[i].part->_maxHealth = healthList[i].currentValue;
        healthList[i].part->flesh = healthList[i].currentValue;
    }



}



float rowHeight = 0.05f;
float rowGap = 0.005f;

void createStatRow(MyGUI::Window* window, float top, float left, StatEntry& stat) {


    MyGUI::TextBox* label = window->getClientWidget()->createWidgetReal<MyGUI::TextBox>("Kenshi_GenericTextBoxSkin", left, top, 0.1f, rowHeight, MyGUI::Align::Center, "Label_" + stat.key);
    label->setFontName("Kenshi_StandardFont_Small");
    label->setCaptionWithReplacing(stat.label);
    label->setTextAlign(MyGUI::Align::Center);

    MyGUI::Button* minusBtn = window->getClientWidget()->createWidgetReal<MyGUI::Button>("Kenshi_Button1", left + 0.1f, top, 0.05f, rowHeight, MyGUI::Align::Center, "Minus_" + stat.key);
    minusBtn->setCaption("-");
    widgetToStat[minusBtn] = &stat;
    minusBtn->eventMouseButtonClick += MyGUI::newDelegate(OnMinusPress);

    MyGUI::TextBox* valueDisplay = window->getClientWidget()->createWidgetReal<MyGUI::TextBox>("Kenshi_GenericTextBoxSkin", left + 0.15f, top, 0.05f, rowHeight, MyGUI::Align::Center, "Value_" + stat.key);
    std::ostringstream valueText;
    std::string suffix = (stat.isStatMod ? "x" : "%");
    valueText << stat.currentValue << suffix;
    valueDisplay->setCaptionWithReplacing(valueText.str());
    valueDisplay->setFontName("Kenshi_StandardFont_Small");
    valueDisplay->setTextAlign(MyGUI::Align::Center);
    stat.valueDisplay = valueDisplay;

    MyGUI::Button* plusBtn = window->getClientWidget()->createWidgetReal<MyGUI::Button>("Kenshi_Button1", left + 0.2f, top, 0.05f, rowHeight, MyGUI::Align::Center, "Plus_" + stat.key);
    plusBtn->setCaption("+");
    widgetToStat[plusBtn] = &stat;
    plusBtn->eventMouseButtonClick += MyGUI::newDelegate(OnPlusPress);




}

void OnMinusHealth(MyGUI::WidgetPtr sender) {

    HealthEntry* stat = widgetToHealth[sender];

    float oldValue = stat->currentValue;
    float newValue = std::max(stat->min, oldValue - stat->step);
    stat->currentValue = newValue;

    if (oldValue != newValue) {
        float priceMult = (newValue < 100 ? stat->refundMult : 1.0f);
        currentPoints += stat->price * priceMult;
    }


    std::ostringstream valueText;
    valueText << stat->currentValue;
    stat->valueDisplay->setCaptionWithReplacing(valueText.str());

    refreshPoints();

}

void OnPlusHealth(MyGUI::WidgetPtr sender) {

    HealthEntry* stat = widgetToHealth[sender];

    float oldValue = stat->currentValue;
    float newValue = std::min(stat->max, oldValue + stat->step);
    stat->currentValue = newValue;

    if (oldValue != newValue) {
        float priceMult = (newValue < 100 ? stat->refundMult : 1.0f);
        currentPoints -= stat->price * priceMult;
    }


    std::ostringstream valueText;
    valueText << stat->currentValue;
    stat->valueDisplay->setCaptionWithReplacing(valueText.str());

    refreshPoints();

}

void createHealthRow(MyGUI::Window* window, float top, float left, HealthEntry& stat) {


    MyGUI::TextBox* label = window->getClientWidget()->createWidgetReal<MyGUI::TextBox>("Kenshi_GenericTextBoxSkin", left, top, 0.1f, rowHeight, MyGUI::Align::Center, "Label_" + stat.key);
    label->setFontName("Kenshi_StandardFont_Small");
    label->setCaptionWithReplacing(stat.label);
    label->setTextAlign(MyGUI::Align::Center);

    MyGUI::Button* minusBtn = window->getClientWidget()->createWidgetReal<MyGUI::Button>("Kenshi_Button1", left + 0.1f, top, 0.05f, rowHeight, MyGUI::Align::Center, "Minus_" + stat.key);
    minusBtn->setCaption("-");
    widgetToHealth[minusBtn] = &stat;
    minusBtn->eventMouseButtonClick += MyGUI::newDelegate(OnMinusHealth);

    MyGUI::TextBox* valueDisplay = window->getClientWidget()->createWidgetReal<MyGUI::TextBox>("Kenshi_GenericTextBoxSkin", left + 0.15f, top, 0.05f, rowHeight, MyGUI::Align::Center, "Value_" + stat.key);
    std::ostringstream valueText;
    valueText << stat.currentValue;
    valueDisplay->setCaptionWithReplacing(valueText.str());
    valueDisplay->setFontName("Kenshi_StandardFont_Small");
    valueDisplay->setTextAlign(MyGUI::Align::Center);
    stat.valueDisplay = valueDisplay;

    MyGUI::Button* plusBtn = window->getClientWidget()->createWidgetReal<MyGUI::Button>("Kenshi_Button1", left + 0.2f, top, 0.05f, rowHeight, MyGUI::Align::Center, "Plus_" + stat.key);
    plusBtn->setCaption("+");
    widgetToHealth[plusBtn] = &stat;
    plusBtn->eventMouseButtonClick += MyGUI::newDelegate(OnPlusHealth);




}

std::map<MyGUI::Widget*, BoolEntry*> widgetToBool;

void onBoolBtnPress(MyGUI::WidgetPtr sender) {

    BoolEntry* boolEntry = widgetToBool[sender];
    MyGUI::Button* boolBtn = sender->castType<MyGUI::Button>();

    currentPoints += boolEntry->currentValue == true ? boolEntry->price : -boolEntry->price;
    boolEntry->currentValue = !boolEntry->currentValue;

    std::string caption = "No";
    if (boolEntry->currentValue) { caption = "Yes"; }

    boolBtn->setCaption(caption);

    refreshPoints();

}




void createBoolRow(MyGUI::Window* window, float top, float left, BoolEntry& boolEntry) {

    MyGUI::TextBox* label = window->getClientWidget()->createWidgetReal<MyGUI::TextBox>("Kenshi_GenericTextBoxSkin", left, top, 0.1f, rowHeight, MyGUI::Align::Center, "Label_" + boolEntry.key);
    label->setFontName("Kenshi_StandardFont_Small");
    label->setCaptionWithReplacing(boolEntry.label);
    label->setTextAlign(MyGUI::Align::Center);

    MyGUI::Button* boolBtn = window->getClientWidget()->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.1f + left, top, 0.05, rowHeight, MyGUI::Align::Center, "Toggle_" + boolEntry.key);
   std::string caption = "No";
    if (boolEntry.currentValue) { caption = "Yes"; }
    boolBtn->setCaption(caption);

    widgetToBool[boolBtn] = &boolEntry;
    boolBtn->eventMouseButtonClick += MyGUI::newDelegate(onBoolBtnPress);



}


void createSectionHeader(MyGUI::Window* window, float top, float left, const std::string& title) {
    MyGUI::TextBox* header = window->getClientWidget()->createWidgetReal<MyGUI::TextBox>(
        "Kenshi_GenericTextBoxSkin", left, top, 0.25f, rowHeight + 0.025, MyGUI::Align::Center, "Header_" + title);
    header->setCaptionWithReplacing(title);
    header->setFontName("Kenshi_StandardFont_Medium");
    header->setTextAlign(MyGUI::Align::Center);
}

float renderCategory(MyGUI::Window* window, float top, float left, const std::string& title, std::vector<StatEntry>& stats) {
    createSectionHeader(window, top, left, title);
    top += rowHeight + 0.025 + rowGap;

    for (size_t i = 0; i < stats.size(); i++) {
        createStatRow(window, top, left, stats[i]);
        top += rowHeight + rowGap;
    }

    return top;
}

void onConfirm(MyGUI::WidgetPtr sender) {

    MyGUI::Button* confirmBtn = sender->castType<MyGUI::Button>();
    if (currentPoints < 0) { confirmBtn->setCaption("INSUFFICIENT"); return; }

    applyAllStats();
    if (!healthList.empty()) { applyHealth(); }

    std::ostringstream pointsText;
    pointsText << currentPoints;
    DebugLog("[PROTOTYPE] Stats confirmed, new points: " + pointsText.str());
    mainWindow->destroySmooth();
    mainWindow = NULL;
    widgetToStat.clear();
    widgetToBool.clear();

}


void createWindowBase(std::string windowType) {

    if (mainWindow != NULL) { // If window already exists, destroy it and create new one with updated displays
        mainWindow->destroySmooth();
        mainWindow = NULL;
        widgetToStat.clear();
        widgetToBool.clear();
    }

    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    mainWindow = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.25, 0.25, 0.75, 0.75, MyGUI::Align::Center, "Window", "TestWindow");

    MyGUI::Button* confirmBtn = mainWindow->getClientWidget()->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.9, 0.95, 0.1, 0.05, MyGUI::Align::Center, "ConfirmButton");
    confirmBtn->setCaption("CONFIRM");
    confirmBtn->eventMouseButtonClick += MyGUI::newDelegate(onConfirm);


    pointsDisplay = mainWindow->getClientWidget()->createWidgetReal<MyGUI::TextBox>("Kenshi_GenericTextBoxSkin", 0.8, 0.95, 0.1, 0.05, MyGUI::Align::Center, "PointsDisplay");
    pointsDisplay->setFontName("Kenshi_StandardFont_Small");
    std::ostringstream pointsText;
    pointsText << currentPoints;
    pointsDisplay->setCaptionWithReplacing(pointsText.str());
    pointsDisplay->setTextAlign(MyGUI::Align::Center);


    if (windowType == "stats") {

        float top = 0.0f;
        for (size_t i = 0; i < statList.size(); i++) {
            StatEntry& stat = statList[i];
            createStatRow(mainWindow, top, 0.0f, stat);
            top += rowHeight + rowGap;
        }

        for (size_t i = 0; i < boolList.size(); i++) {
            BoolEntry& boolEntry = boolList[i];
            createBoolRow(mainWindow, top, 0.0f, boolEntry);
            top += rowHeight + rowGap;
        }


        top = 0.0f;
        top = renderCategory(mainWindow, top, 0.25f, "Attributes", attributeStats);
        top = renderCategory(mainWindow, top, 0.25f, "Athletics/Swimming", athleticStats);
        top = renderCategory(mainWindow, top, 0.25f, "Weapons", weaponStats);
        top = renderCategory(mainWindow, top, 0.25f, "Combat", combatStats);
        top = renderCategory(mainWindow, top, 0.25f, "Ranged", rangedStats);
        top = renderCategory(mainWindow, top, 0.25f, "Precision", precisionStats);
        top = 0.0f;
        top = renderCategory(mainWindow, top, 0.5f, "Stealth", stealthStats);
        top = renderCategory(mainWindow, top, 0.5f, "Sciences", scienceStats);
        top = renderCategory(mainWindow, top, 0.5f, "Smithing", smithingStats);
        top = renderCategory(mainWindow, top, 0.5f, "Labor", laborStats);

    }

    if (windowType == "health") {

        float top = 0.0f;
        for (size_t i = 0; i < healthList.size(); i++) {
            HealthEntry& stat = healthList[i];
            createHealthRow(mainWindow, top, 0.0f, stat);
            top += rowHeight + rowGap;
        }


    }
}

//------------SAVE/LOAD LOGIC---------------//

int characterStartLine;

void saveAllPrototypeHealth(std::ofstream& file) {
    PlayerInterface* pi = ou->player;
    lektor<Character*> allCharacters = pi->getAllPlayerCharacters();

    for (size_t i = 0; i < allCharacters.size(); i++) {
        Character* c = allCharacters[i];
        if (c->getRace() != protoRace) { 
            //DebugLog("Not prototype didn't save"); 
            continue;
        }

        int charID = c->data->id;
        if (charID == NULL) { DebugLog("No id didn't save"); continue; }
        createHealthList(c->getMedical());
        if (healthList.empty()) { DebugLog("No list didn't save"); continue; }

        file << "[Character:" << charID << "]\n";
        for (size_t j = 0; j < healthList.size(); j++) {
            file << healthList[j].key << "=" << healthList[j].currentValue << "\n";
        }
    }
}

void saveCategory(std::ofstream& file, std::vector<StatEntry>& category) {
    for (size_t i = 0; i < category.size(); i++) {
        file << category[i].key << "=" << category[i].currentValue << "\n";
    }
}

void saveBoolCategory(std::ofstream& file) {
    for (size_t i = 0; i < boolList.size(); i++) {
        file << boolList[i].key << "=" << (boolList[i].currentValue ? 1 : 0) << "\n";
    }
}

// After an ini is found or created by GetSaveDataPath, values are written into it as "stat.key=stat.currentValue"

static void SaveCustomization(const std::string& uid) {
    std::string path = GetSaveDataPath(uid);
    std::ofstream file(path.c_str(), std::ios::trunc);
    if (!file.is_open()) return;

    file << "[Globals]\n";
    file << "currentPoints=" << currentPoints << "\n";
    file << "[PrototypeStats]\n";
    for (size_t i = 0; i < statList.size(); i++) {
        file << statList[i].key << "=" << statList[i].currentValue << "\n";
    }
    saveBoolCategory(file);

    saveCategory(file, attributeStats);
    saveCategory(file, athleticStats);
    saveCategory(file, weaponStats);
    saveCategory(file, combatStats);
    saveCategory(file, rangedStats);
    saveCategory(file, stealthStats);
    saveCategory(file, scienceStats);
    saveCategory(file, smithingStats);
    saveCategory(file, laborStats);

    saveAllPrototypeHealth(file);
}

// Find entry by key so that value can be loaded

HealthEntry* findHealthByKey(const std::string& key) {
    for (size_t i = 0; i < healthList.size(); i++) {
        if (healthList[i].key == key) return &healthList[i];
    }
    return NULL;


}

BoolEntry* findBoolByKey(const std::string& key) {
    for (size_t i = 0; i < boolList.size(); i++) {
        if (boolList[i].key == key) return &boolList[i];
    }
    return NULL;
}

StatEntry* findStatByKey(const std::string& key) {
    std::vector<StatEntry>* allCategories[] = {
      &attributeStats, &athleticStats, &weaponStats, &combatStats, &rangedStats, &stealthStats, &scienceStats, &smithingStats, &laborStats, &statList
    };

    for (int c = 0; c < 10; c++) {  // Seems like the ancient 2010 toolset doesn't have a way to get the size of allCategories[], so we manually put it in for loop
        std::vector<StatEntry>& category = *allCategories[c];
        for (size_t i = 0; i < category.size(); i++) {
            if (category[i].key == key) return &category[i];
        }
    }
    DebugLog("Couldn't find " + key);
    return NULL;
}

// Each line, look at the key, find the stat with the matching key, then look at the value, and assign that value to that stat

void LoadCustomizationHealth(const std::string& uid, int characterStartLine) {
    if (characterStartLine < 1) return;

    std::ifstream file(GetSaveDataPath(uid).c_str());
    if (!file.is_open()) return;

    PlayerInterface* pi = ou->player;
    lektor<Character*> allCharacters = pi->getAllPlayerCharacters();

    std::string currentSection;
    Character* sectionCharacter = NULL;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;

        // The first function records this line as the start of character data.
        if (lineNumber < characterStartLine) continue;

        if (!line.empty() && line[0] == '[') {
            currentSection.clear();
            sectionCharacter = NULL;

            if (line.size() >= 2 && line[line.size() - 1] == ']') {
                currentSection = line.substr(1, line.size() - 2);
            }

            if (currentSection.compare(0, 10, "Character:") == 0) {
                const std::string charID = currentSection.substr(10);

                for (size_t i = 0; i < allCharacters.size(); ++i) {
                    Character* c = allCharacters[i];
                    if (!c || c->getRace() != protoRace || !c->data) continue;

                    std::stringstream idStream;
                    idStream << c->data->id;

                    if (idStream.str() != charID) continue;

                    sectionCharacter = c;
                    createHealthList(c->getMedical());
                    break;
                }
            }

            continue;
        }

        if (currentSection.compare(0, 10, "Character:") != 0 || !sectionCharacter) {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        const std::string valueStr = line.substr(eq + 1);

        HealthEntry* healthEntry = findHealthByKey(key);
        if (healthEntry && healthEntry->part) {
            healthEntry->part->_maxHealth = (float)atof(valueStr.c_str());
        }
    }
}

void LoadCustomization(const std::string& uid) {
    std::ifstream file(GetSaveDataPath(uid).c_str());
    if (!file.is_open()) {
        createWindowBase("stats");
        return;
    }

    std::string line;
    int lineNumber = 0;
    characterStartLine = -1;

    while (std::getline(file, line)) {
        ++lineNumber;

        if (!line.empty() && line[0] == '[') {
            if (line.size() >= 2 && line[line.size() - 1] == ']') {
                const std::string section = line.substr(1, line.size() - 2);

                if (section.compare(0, 10, "Character:") == 0) {
                    characterStartLine = lineNumber;
                    break;
                }
            }

            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        const std::string valueStr = line.substr(eq + 1);

        if (key == "currentPoints") {
            currentPoints = (float)atof(valueStr.c_str());
            DebugLog("Loaded: Points, " + valueStr);
            continue;
        }

        BoolEntry* boolEntry = findBoolByKey(key);
        if (boolEntry) {
            boolEntry->currentValue = (atoi(valueStr.c_str()) != 0);
            DebugLog("Loaded: " + boolEntry->key + ", " + valueStr);
        }

        StatEntry* stat = findStatByKey(key);
        if (stat) {
            stat->currentValue = (float)atof(valueStr.c_str());
            DebugLog("Loaded: " + stat->key + ", " + valueStr);
        }
    }

    file.close();

    applyAllStats();
}


void (*serialise_orig)(PlayerInterface*, GameData*) = NULL;
void serialise_hook(PlayerInterface* thisptr, GameData* data)
{
    serialise_orig(thisptr, data);

    DebugLog(data->stringID);
    SaveCustomization(data->stringID); // Hook gives us GameData, use its stringID to differentiate saves





}

bool firstLoad = true;
bool tryingLoad = false;
bool gameStart = true;
std::string currentDataID;

TimeOfDay timeOfDay;

void firstLoadProcedure() {
    timeOfDay = ou->getTimeStamp_inGameHours();
    createStatList();
    createBoolList();
    createStatModList();

    createWindowBase("stats");
    gameStart = false;
    firstLoad = false;


}

void (*loadFromSerialise_orig)(PlayerInterface*, GameData*) = NULL;
void loadFromSerialise_hook(PlayerInterface* thisptr, GameData* data)
{
    gameStart = false; // If a save was loaded that means it's not the start of the playthrough

    // Setting these to default before load so they don't become dangling pointers and cause a crash
    extractActor = NULL;
    g_extractTarget = NULL;
    travelingToTarget = false;
    playingExtractAnim = false;

    

    loadFromSerialise_orig(thisptr, data);
   

    DebugLog(data->stringID);
    currentDataID = data->stringID;
    timeOfDay = ou->getTimeStamp_inGameHours(); // Seems like a weird way to get a TimeOfDay instance but it works I guess

    if (firstLoad) {
        firstLoadProcedure();
    }

    LoadCustomization(currentDataID);
    tryingLoad = true; // Starts a check in main world loop hook


    
}

void tryingLoadLoop() {
    // Have to do this because getAllPlayerCharacters() isn't populated yet on the load hook
    if (tryingLoad && ou->player->getAllPlayerCharacters().size() > 0) {
        LoadCustomizationHealth(currentDataID, characterStartLine);
        tryingLoad = false;
    }
}



void (*_doActions_orig)(Dialogue* thisptr, DialogLineData* dialogLine);
void _doActions_hook(Dialogue* thisptr, DialogLineData* dialogLine)
{

    _doActions_orig(thisptr, dialogLine);
    // Added a custom "action" (more like a flag that doesn't do anything) to the fcs using FCS Extended and added a dialogue option that has it to the Black Desert City robotics vendor.
    // On the code end here, this checks if a dialogue has the "action" by searching for its name
    ogre_unordered_map<std::string, Ogre::vector<GameDataReference>::type>::type::iterator iter = dialogLine->getGameData()->objectReferences.find("open prototype menu");
    // If it does, the edit window is created:
    if (iter != dialogLine->getGameData()->objectReferences.end())
        createWindowBase("stats");

    ogre_unordered_map<std::string, Ogre::vector<GameDataReference>::type>::type::iterator iter2 = dialogLine->getGameData()->objectReferences.find("open health menu");

    if (iter2 != dialogLine->getGameData()->objectReferences.end()) {
        Character* speaker = ou->player->selectedCharacter.getCharacter();

        createHealthList(speaker->getMedical());
        createWindowBase("health");
    }
}

MyGUI::ProgressBar* createExtractBar() {

    if (extractBar != NULL) {
        MyGUI::Gui::getInstancePtr()->destroyWidget(extractBar);
        extractBar = NULL;
    }

    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();

    MyGUI::ProgressBar* progressBar = gui->createWidgetReal<MyGUI::ProgressBar>("Kenshi_ProgressBarFill", 0.4, 0.725, 0.2, 0.025, MyGUI::Align::Center, "Back", "Extract_Bar");


    progressBar->setProgressRange(256);
    progressBar->setFlowDirection(MyGUI::FlowDirection::LeftToRight);
    progressBar->setVisible(true);
    

    DebugLog("Extract bar created");

    return progressBar;
}



Item* findExtractorIfHas(Character* character) {

    Inventory* charInv = character->getInventory();

    lektor<Item*> foundItems;
    lektor<InventorySection*> mainInv = charInv->getAllSections();

    for (size_t i = 0; i < mainInv.size(); i++) {
        mainInv[i]->getAllItemsOfName(foundItems, "Data Extractor");
        if (foundItems.size() > 0) { return foundItems[0]; }
    }

    ContainerItem* backpack = character->hasABackpackOn();
    if (backpack) {
        Inventory* bpInv = backpack->inventory;
        if (bpInv) {
            lektor<InventorySection*> bpSecs = bpInv->getAllSections();

            for (size_t i = 0; i < bpSecs.size(); i++) {
                bpSecs[i]->getAllItemsOfName(foundItems, "Data Extractor");
                if (foundItems.size() > 0) { return foundItems[0]; }
            }
        }

    }
    
    return NULL;


}


ScreenLabel* extractedPointsLabel;

void awardPoints() {

    float reward = g_extractReward;
    if (g_extractTarget->isDead()) { reward /= 2; }


    currentPoints += reward;

    std::stringstream pointsText;
    pointsText << currentPoints;
    DebugLog("Data extracted, current points: " + pointsText.str());

    std::stringstream rewardText;
    rewardText << "+" << reward << " points! " << "(" << currentPoints << ")";

    extractedPointsLabel = gui->createScreenLabel(rewardText.str(), MyGUI::Colour(0, 1, 0, 1), ScreenLabel::LS_SMALL, ScreenLabel::RS_SLOW);
    extractedPointsLabel->setTracking(extractActor->handle, Ogre::Vector3(0, 0, 0));
    extractedPointsLabel->setVisible(true);
}

void endExtraction() {

    playingExtractAnim = false;
    gui->destroyWidget(extractBar);
    extractBar = NULL;

}


void doExtractSequence() {
    
    if (!g_extractTarget->isDead() && !g_extractTarget->isLiterallyUnconciousNotPretending()) { endExtraction(); return; }
    
    if (findExtractorIfHas(extractActor) == NULL) { endExtraction(); return; }

    // Prevents running away while extracting by carrying target while still enabling carrying if standing still
    // Small grace period just in case it accidentally gets triggered while coming to a stop
    if (timeOfDay.getMinutesPassed() - now > 1.5 && extractActor->getMovement()->isCurrentlyMoving()) { endExtraction(); return; }




    if ((timeOfDay.getMinutesPassed() - now) > 8) { 
        
        endExtraction();



        Item* extractor = findExtractorIfHas(extractActor);
        if (extractor != NULL) {
            extractor->chargesLeft -= 1;
            if (extractor->chargesLeft < 1) { 
                // Extractor isn't null, so if it doesn't get destroyed from inventory, it must be in backpack
                // I tried destroying it from extractor->getInventory to simplify things but it caused crashes
                if (!extractActor->getInventory()->removeItemAutoDestroy(extractor, 1)) { extractActor->hasABackpackOn()->inventory->removeItemAutoDestroy(extractor, 1); }
                extractor = NULL; 
            }
        }
        else { return; }

        awardPoints();
        Damages headDamage(0, (extractTargetHead->maxHealth() * percentHeadDamage), 0, 0, 0);
        extractTargetHead->applyDamage(headDamage);

        return; 
    }
    else { 
        // Increased bar range and multiplied this to make it smoother, not very clean though, should automate it
        extractBar->setProgressPosition(floor(((timeOfDay.getMinutesPassed() - now) * 32) + 0.5)); // Used floor(... + 0.5) because the 2010 toolset doesn't seem to have a normal way to round
        
    }
    
    
    // Animation has to be forced every frame, otherwise it stops instantly
    extractActor->getAnimationClass()->runAnimation(medicAnim, 1.0f, medicAnim->layername, 1.0f);

}

void onExtractClicked(MyGUI::WidgetPtr sender) {

    if (ou != NULL && ou->player != NULL) {
        extractActor = ou->player->selectedCharacter.getCharacter();
        if (extractActor == NULL) extractActor = ou->player->getAnyPlayerCharacter();
    }

    if (findExtractorIfHas(extractActor) == NULL) { return; }

    Character* target = g_extractTarget;
    extractTargetHead = target->getMedical()->getPart(MedicalSystem::HealthPartStatus::PartType::PART_HEAD, LeftRight::SIDE_NEITHER);
    bool isDead = target->isDead();

    if (!isDead && !target->isLiterallyUnconciousNotPretending()) { return; }

    extractActor->addOrder(NULL, MOVE_CUS_ORDERED, NULL, false, false, target->getPosition());
    AnimationData* ad = extractActor->getAnimationClass()->getAnimationData("medic"); // First aid animation is most fitting I could find, name string for lookup can be found in FCS
    medicAnim = ad;
    if (ad) {
        travelStartTime = timeOfDay.getMinutesPassed();
        travelingToTarget = true;
    }

}

void travelingToTargetSequence() {

    if (travelingToTarget && extractActor != NULL && g_extractTarget != NULL) {
        if (extractActor->getPosition().squaredDistance(g_extractTarget->getPosition()) <= extractRadius) {
            travelingToTarget = false;
            playingExtractAnim = true;
            now = timeOfDay.getMinutesPassed();

            extractBar = createExtractBar();
            extractBar->setProgressPosition(0);
        }
        else if (timeOfDay.getMinutesPassed() - travelStartTime > 15) {

            travelingToTarget = false;
        }
        return;
    }

}

void initiateExtraction() {
    if (playingExtractAnim && extractActor != NULL && g_extractTarget != NULL) {
        if (extractActor->getPosition().squaredDistance(g_extractTarget->getPosition()) <= extractRadius) {
            doExtractSequence();
        }
        else {
            endExtraction();
        }
    }

}

// Most of context menu code is taken from Disarm https://github.com/dafitime/disarm/blob/master/Disarm.cpp

void (*ContextMenu_show_orig)(ContextMenu*, bool, RootObject*) = NULL;

void ContextMenu_show_hook(ContextMenu* thisptr, bool on, RootObject* what)
{
    g_activeMenu = thisptr;
    CleanupCustomUI();
    ContextMenu_show_orig(thisptr, on, what);
    if (!on || thisptr == NULL) return;

    ContextMenuGUI* candidates[2] = { thisptr->menuGUI, thisptr->menuGUI2 };
    ContextMenuGUI* gui = NULL;
    Character* target = NULL;
    Character* actor = ou->player->selectedCharacter.getCharacter();
    
    if (findExtractorIfHas(actor) == NULL) { return; }

    for (int i = 0; i < 2; ++i) {
        ContextMenuGUI* c = candidates[i];
        if (c == NULL || c->optionsList == NULL) continue;
        Character* ch = c->contextMenuTarget.getCharacter();
        if (ch != NULL) { gui = c; target = ch; break; }
    }
    if (target == NULL) {
        for (int i = 0; i < 2; ++i) {
            ContextMenuGUI* c = candidates[i];
            if (c == NULL || c->optionsList == NULL) continue;
            Building* bld = c->contextMenuTarget.getBuilding();
            if (bld == NULL) continue;
            UseableStuff* u = bld->getUseableStuff();
            if (u == NULL) continue;
            Character* occ = u->getOccupant().getCharacter();
            if (occ != NULL) { gui = c; target = occ; break; }
        }
    }


    if (gui == NULL || target == NULL) return;

    // Always correct mMainWidget to the right size for the current menu content.
    // Runs even when Disarm is not applicable so other NPCs don't get a gap.
    // g_mainListDiff is the constant decoration offset (frame/title/padding).
    if (g_mainListDiff >= 0 && gui->mMainWidget != NULL && gui->optionsList != NULL
        && gui->mMainWidget != gui->optionsList) {
        int correctH = gui->optionsList->getSize().height + g_mainListDiff;
        gui->mMainWidget->setSize(
            MyGUI::IntSize(gui->mMainWidget->getSize().width, correctH));
    }

    if (!target->getRace()->isRelatedRace(skeletonRace)) { return; }
    if (!target->isDead() && !target->isLiterallyUnconciousNotPretending()) { return; }
    if (target->isAnimal() || target->isPlayerCharacter()) { return; }
    g_extractTarget = target;



    MyGUI::Widget* list = gui->optionsList;
    size_t n = list->getChildCount();
    if (n == 0) return;

    MyGUI::Widget* lastRow = list->getChildAt(n - 1);
    MyGUI::IntCoord pc = lastRow->getCoord();
    int             rowH = pc.height > 0 ? pc.height : 30;
    int             btnHeight = rowH - 3;
    if (btnHeight < 1) btnHeight = rowH;
    int             buttonTop = pc.top + rowH;  // flush with row start, no centering gap

    int btnLeft = pc.left;
    int btnWidth = pc.width;
    if (gui->buttonCoords.width > 0) {
        btnLeft = pc.left + gui->buttonCoords.left;
        btnWidth = gui->buttonCoords.width;  // no extra, matches other buttons exactly
    }

    // Percentage value column: mirrors the live "Value" child of the game's
// own Option template (gui->valueCoords). This is populated straight from
// whichever context-menu skin is actually active, so it automatically
// matches vanilla (a separate, non-overlapping value box after a shorter
// button) or a themed skin like Dark UI (a wider button with the value
// column overlapping it as a right-aligned overlay) without needing to
// detect any specific mod by name.
    int valLeft = btnLeft;
    int valWidth = btnWidth;
    if (gui->valueCoords.width > 0) {
        valLeft = pc.left + gui->valueCoords.left;
        valWidth = gui->valueCoords.width;
    }
    // This is what decides which visual style (overlay vs. separate column)
    // is active.
    bool valOverlapsButton = valLeft < (btnLeft + btnWidth);



    if (!valOverlapsButton && gui->buttonCoords.width > 0) {
        for (size_t i = 0; i < n; ++i) {
            MyGUI::Widget* row = list->getChildAt(i);
            if (row == NULL || row->getChildCount() == 0) continue;
            MyGUI::Widget* rowBtn = row->getChildAt(0);
            if (rowBtn == NULL) continue;
            MyGUI::IntCoord rbc = rowBtn->getCoord();
            if (rbc.left != gui->buttonCoords.left || rbc.width != gui->buttonCoords.width) {
                rowBtn->setCoord(gui->buttonCoords.left, rbc.top, gui->buttonCoords.width, rbc.height);
            }
        }
    }



    // ---- Determine parent for our buttons ----
// We add to mMainWidget (NOT optionsList) so the game's rebuild of
// optionsList never touches our widgets and game buttons are never eaten.
    MyGUI::Widget* btnParent = (gui->mMainWidget != NULL) ? gui->mMainWidget : list;

    // Walk up from list to btnParent accumulating the coordinate offset.
    int absOffX = 0, absOffY = 0;
    for (MyGUI::Widget* cur = list; cur != NULL && cur != btnParent; cur = cur->getParent()) {
        MyGUI::IntCoord cc = cur->getCoord();
        absOffX += cc.left;
        absOffY += cc.top;
    }
    int absButtonTop = absOffY + buttonTop;
    int absBtnLeft = absOffX + btnLeft;
    int absValLeft = absOffX + valLeft;

    std::string    btnName = "ExtractDataBtn";
    MyGUI::Button* btn = btnParent->createWidget<MyGUI::Button>(
        "Kenshi_Button1",
        MyGUI::IntCoord(absBtnLeft, absButtonTop, btnWidth, btnHeight),
        MyGUI::Align::Default, btnName);
    btn->setCaption("Extract Data");
    btn->setTextAlign(MyGUI::Align::Center);
    btn->setNeedMouseFocus(true);
    btn->eventMouseButtonClick += MyGUI::newDelegate(onExtractClicked);
    g_customWidgets.push_back(btn);


    // ---- Grow only mMainWidget ----
// Capture decoration offset (mMainWidget.height - optionsList.height) once.
// The per-show correction above already forced mMainWidget to the correct
// natural size, so we just read and grow from there.
    if (g_mainListDiff < 0 && btnParent != list)
        g_mainListDiff = btnParent->getSize().height - list->getSize().height;

    int growBy = rowH + 2;
    {
        MyGUI::IntSize sz = btnParent->getSize();
        StretchedWidget sw;
        sw.widget = btnParent;
        sw.originalHeight = sz.height;
        sw.targetHeight = sz.height + growBy;
        btnParent->setSize(MyGUI::IntSize(sz.width, sw.targetHeight));
        g_stretchedParents.push_back(sw);
    }


    g_extractRowActive = true;
}

void (*GameWorld_mainLoop_orig)(GameWorld*, float);

void GameWorld_mainLoop_hook(GameWorld* thisptr, float time)
{
    GameWorld_mainLoop_orig(thisptr, time);

    // For when a new game is created, hence no load from serialise hook to use
    if (gameStart && ou->player->getAllPlayerCharacters().size() > 0) {
        firstLoadProcedure();
    }
    tryingLoadLoop();

    travelingToTargetSequence();
    initiateExtraction();
}





__declspec(dllexport) void startPlugin()
{
	DebugLog("Hello world!");

	KenshiLib::AddHook(KenshiLib::GetRealAddress(&PlayerInterface::loadFromSerialise), loadFromSerialise_hook, &loadFromSerialise_orig);
    KenshiLib::AddHook(KenshiLib::GetRealAddress(&PlayerInterface::serialise), serialise_hook, &serialise_orig);
    KenshiLib::AddHook(KenshiLib::GetRealAddress(&ContextMenu::showContextMenu), &ContextMenu_show_hook, &ContextMenu_show_orig);
    KenshiLib::AddHook(KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff), &GameWorld_mainLoop_hook, &GameWorld_mainLoop_orig);
    KenshiLib::AddHook(KenshiLib::GetRealAddress(&Dialogue::_doActions), &_doActions_hook, &_doActions_orig);
    //KenshiLib::AddHook(KenshiLib::GetRealAddress(&FactionManager::saveGameState), &saveGameState_hook, &saveGameState_orig);
}