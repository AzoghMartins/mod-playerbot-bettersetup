/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"

#include "AiFactory.h"
#include "ChatFilter.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "RandomItemMgr.h"
#include "StatsWeightCalculator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
/* File map for tired mortals and any future maintainer who was told
 * this would be "a quick tweak" five merges ago:
 * 1) Command parsing and token wrangling.
 * 2) Class spec catalogs and resolver logic.
 * 3) Expansion/talent caps and post-spec refresh work.
 * 4) Gear policy, target collection, and chat hooks.
 */

constexpr char const* CONF_SPEC_ENABLE = "PlayerbotBetterSetup.Spec.Enable";
constexpr char const* CONF_REQUIRE_MASTER_CONTROL = "PlayerbotBetterSetup.Spec.RequireMasterControl";
constexpr char const* CONF_SHOW_SPEC_LIST_ON_EMPTY = "PlayerbotBetterSetup.Spec.ShowSpecListOnEmpty";
constexpr char const* CONF_AUTO_GEAR_RNDBOTS = "PlayerbotBetterSetup.Spec.AutoGearRndBots";
constexpr char const* CONF_AUTO_GEAR_ALTBOTS = "PlayerbotBetterSetup.Spec.AutoGearAltBots";
constexpr char const* CONF_GEAR_MODE_RNDBOTS = "PlayerbotBetterSetup.Spec.GearModeRndBots";
constexpr char const* CONF_GEAR_MODE_ALTBOTS = "PlayerbotBetterSetup.Spec.GearModeAltBots";
constexpr char const* CONF_GEAR_RATIO_RNDBOTS = "PlayerbotBetterSetup.Spec.GearMasterIlvlRatioRndBots";
constexpr char const* CONF_GEAR_RATIO_ALTBOTS = "PlayerbotBetterSetup.Spec.GearMasterIlvlRatioAltBots";
constexpr char const* CONF_EXPANSION_SOURCE = "PlayerbotBetterSetup.Spec.ExpansionSource";
constexpr char const* CONF_GEAR_VALIDATION_LOWER_RATIO = "PlayerbotBetterSetup.Spec.GearValidationLowerRatio";
constexpr char const* CONF_GEAR_VALIDATION_UPPER_RATIO = "PlayerbotBetterSetup.Spec.GearValidationUpperRatio";
constexpr char const* CONF_GEAR_RETRY_COUNT = "PlayerbotBetterSetup.Spec.GearRetryCount";
constexpr char const* CONF_GEAR_QUALITY_CAP_RATIO_MODE = "PlayerbotBetterSetup.Spec.GearQualityCapRatioMode";
constexpr char const* CONF_GEAR_QUALITY_CAP_TOP_FOR_LEVEL = "PlayerbotBetterSetup.Spec.GearQualityCapTopForLevel";
constexpr char const* CONF_SPECPLAYER_MIN_SECURITY = "PlayerbotBetterSetup.SpecPlayer.MinSecurityLevel";
constexpr char const* CONF_SPECPLAYER_ALLOW_OFFLINE_QUEUE = "PlayerbotBetterSetup.SpecPlayer.AllowOfflineQueue";
constexpr char const* CONF_SPECPLAYER_APPLY_PRIMARY_PROFESSIONS = "PlayerbotBetterSetup.SpecPlayer.ApplyPrimaryProfessions";
constexpr char const* CONF_SPECPLAYER_NORMALIZE_KNOWN_SKILLS = "PlayerbotBetterSetup.SpecPlayer.NormalizeKnownSkills";
constexpr char const* CONF_SPECPLAYER_NORMALIZE_RIDING = "PlayerbotBetterSetup.SpecPlayer.NormalizeRiding";
constexpr char const* CONF_SPECPLAYER_RIDING_BASIC_LEVEL = "PlayerbotBetterSetup.SpecPlayer.RidingBasicLevel";
constexpr char const* CONF_SPECPLAYER_RIDING_BASIC_SKILL = "PlayerbotBetterSetup.SpecPlayer.RidingBasicSkill";
constexpr char const* CONF_SPECPLAYER_RIDING_ADVANCED_LEVEL = "PlayerbotBetterSetup.SpecPlayer.RidingAdvancedLevel";
constexpr char const* CONF_SPECPLAYER_RIDING_ADVANCED_SKILL = "PlayerbotBetterSetup.SpecPlayer.RidingAdvancedSkill";
constexpr char const* CONF_SPECPLAYER_REMOVE_LEVEL60_EPIC_CLASS_MOUNT_SPELLS =
    "PlayerbotBetterSetup.SpecPlayer.RemoveLevel60EpicClassMountSpells";
constexpr char const* CONF_SPECPLAYER_POST_GLYPHS = "PlayerbotBetterSetup.SpecPlayer.PostGlyphs";
constexpr char const* CONF_SPECPLAYER_POST_CONSUMABLES = "PlayerbotBetterSetup.SpecPlayer.PostConsumables";
constexpr char const* CONF_SPECPLAYER_POST_PET = "PlayerbotBetterSetup.SpecPlayer.PostPet";
constexpr char const* CONF_SPECPLAYER_POST_PET_TALENTS = "PlayerbotBetterSetup.SpecPlayer.PostPetTalents";
constexpr char const* CONF_SPECPLAYER_TARGET_AVERAGE_ILVL_60 = "PlayerbotBetterSetup.SpecPlayer.TargetAverageIlvl60";
constexpr char const* CONF_SPECPLAYER_TARGET_AVERAGE_ILVL_70 = "PlayerbotBetterSetup.SpecPlayer.TargetAverageIlvl70";
constexpr char const* CONF_SPECPLAYER_GEAR_RETRY_COUNT = "PlayerbotBetterSetup.SpecPlayer.GearRetryCount";
constexpr char const* CONF_SPECPLAYER_GEAR_QUALITY_CAP = "PlayerbotBetterSetup.SpecPlayer.GearQualityCap";
constexpr char const* CONF_SPECPLAYER_EXCLUDE_QUEST_REWARD_ITEMS = "PlayerbotBetterSetup.SpecPlayer.ExcludeQuestRewardItems";
constexpr char const* CONF_SPECPLAYER_ENFORCE_UNIQUE_RING_TRINKET_PAIRS =
    "PlayerbotBetterSetup.SpecPlayer.EnforceUniqueRingTrinketPairs";
constexpr char const* CONF_SPECPLAYER_GEAR_LEVEL_SEARCH_WINDOW = "PlayerbotBetterSetup.SpecPlayer.GearLevelSearchWindow";
constexpr char const* CONF_LOGIN_DIAGNOSTICS_ENABLE = "PlayerbotBetterSetup.LoginDiagnostics.Enable";
constexpr char const* OFFLINE_SPECPLAYER_SOURCE = "mod-playerbot-bettersetup-specplayer";
constexpr char const* PET_SPEC_SOURCE = "mod-playerbot-bettersetup-petspec";
constexpr uint32 SPELL_RIGHTEOUS_FURY = 25780;
constexpr uint32 SPELL_RIGHTEOUS_FURY_THREAT_PASSIVE = 57340;

/* These helpers do the civil-service work:
 * players speak in accents, shortcuts, and optimism; code wants exact tokens.
 */

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

std::string TrimCopy(std::string value)
{
    auto const isSpace = [](unsigned char c) { return std::isspace(c) != 0; };

    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

bool StartsWith(std::string const& value, std::string const& prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string NormalizeToken(std::string const& input)
{
    std::string out;
    out.reserve(input.size());

    for (unsigned char c : input)
    {
        if (std::isalnum(c))
            out.push_back(static_cast<char>(std::tolower(c)));
    }

    return out;
}

std::vector<std::string> SplitCommands(std::string const& input, std::string const& separator)
{
    if (separator.empty())
        return { input };

    std::vector<std::string> commands;
    size_t begin = 0;

    while (begin <= input.size())
    {
        size_t pos = input.find(separator, begin);
        if (pos == std::string::npos)
        {
            commands.push_back(input.substr(begin));
            break;
        }

        commands.push_back(input.substr(begin, pos - begin));
        begin = pos + separator.size();
    }

    return commands;
}

std::vector<std::string> SplitWords(std::string const& input)
{
    std::vector<std::string> tokens;
    std::istringstream stream(input);
    std::string word;

    while (stream >> word)
        tokens.push_back(word);

    return tokens;
}

std::string JoinWords(std::vector<std::string> const& words, size_t from)
{
    if (from >= words.size())
        return "";

    std::ostringstream out;
    for (size_t i = from; i < words.size(); ++i)
    {
        if (i != from)
            out << ' ';

        out << words[i];
    }

    return out.str();
}

using ProfessionPair = std::pair<uint16, uint16>;

std::array<uint16, 11> const& GetPrimaryProfessionSkillIds()
{
    static std::array<uint16, 11> primaryProfessionSkillIds = {
        SKILL_ALCHEMY,
        SKILL_BLACKSMITHING,
        SKILL_ENCHANTING,
        SKILL_ENGINEERING,
        SKILL_HERBALISM,
        SKILL_INSCRIPTION,
        SKILL_JEWELCRAFTING,
        SKILL_LEATHERWORKING,
        SKILL_MINING,
        SKILL_SKINNING,
        SKILL_TAILORING
    };

    return primaryProfessionSkillIds;
}

std::array<uint16, 3> const& GetSecondaryProfessionSkillIds()
{
    static std::array<uint16, 3> secondaryProfessionSkillIds = {
        SKILL_FIRST_AID,
        SKILL_FISHING,
        SKILL_COOKING
    };

    return secondaryProfessionSkillIds;
}

struct SecondaryProfessionRankSpell
{
    uint16 requiredMaxSkill;
    uint32 spellId;
};

std::array<SecondaryProfessionRankSpell, 6> const& GetSecondaryProfessionRankSpells(uint16 skillId)
{
    static std::array<SecondaryProfessionRankSpell, 6> firstAidRankSpells = {
        SecondaryProfessionRankSpell{ 1, 3273 },
        SecondaryProfessionRankSpell{ 76, 3274 },
        SecondaryProfessionRankSpell{ 151, 7924 },
        SecondaryProfessionRankSpell{ 226, 10846 },
        SecondaryProfessionRankSpell{ 301, 27028 },
        SecondaryProfessionRankSpell{ 376, 45542 }
    };
    static std::array<SecondaryProfessionRankSpell, 6> fishingRankSpells = {
        SecondaryProfessionRankSpell{ 1, 7620 },
        SecondaryProfessionRankSpell{ 76, 7731 },
        SecondaryProfessionRankSpell{ 151, 7732 },
        SecondaryProfessionRankSpell{ 226, 18248 },
        SecondaryProfessionRankSpell{ 301, 33095 },
        SecondaryProfessionRankSpell{ 376, 51294 }
    };
    static std::array<SecondaryProfessionRankSpell, 6> cookingRankSpells = {
        SecondaryProfessionRankSpell{ 1, 2550 },
        SecondaryProfessionRankSpell{ 76, 3102 },
        SecondaryProfessionRankSpell{ 151, 3413 },
        SecondaryProfessionRankSpell{ 226, 18260 },
        SecondaryProfessionRankSpell{ 301, 33359 },
        SecondaryProfessionRankSpell{ 376, 51296 }
    };

    switch (skillId)
    {
        case SKILL_FIRST_AID:
            return firstAidRankSpells;
        case SKILL_FISHING:
            return fishingRankSpells;
        case SKILL_COOKING:
        default:
            return cookingRankSpells;
    }
}

std::array<uint32, 4> const& GetRidingRankSpellIds()
{
    static std::array<uint32, 4> ridingRankSpellIds = {
        33388,
        33391,
        34090,
        34091
    };

    return ridingRankSpellIds;
}

struct RidingStateSnapshot
{
    bool hadSkill = false;
    uint16 step = 0;
    uint16 value = 0;
    uint16 maxValue = 0;
    std::array<bool, 4> knownRankSpells = {};
};

bool IsPrimaryProfessionSkillId(uint32 skillId)
{
    auto const& primaryProfessionSkillIds = GetPrimaryProfessionSkillIds();
    return std::find(primaryProfessionSkillIds.begin(), primaryProfessionSkillIds.end(), skillId) != primaryProfessionSkillIds.end();
}

bool IsSecondaryProfessionSkillId(uint32 skillId)
{
    auto const& secondaryProfessionSkillIds = GetSecondaryProfessionSkillIds();
    return std::find(secondaryProfessionSkillIds.begin(), secondaryProfessionSkillIds.end(), skillId) != secondaryProfessionSkillIds.end();
}

std::unordered_map<std::string, uint16> const& GetProfessionAliases()
{
    static std::unordered_map<std::string, uint16> aliases = {
        { "alchemy", SKILL_ALCHEMY },
        { "alch", SKILL_ALCHEMY },
        { "blacksmithing", SKILL_BLACKSMITHING },
        { "blacksmith", SKILL_BLACKSMITHING },
        { "bs", SKILL_BLACKSMITHING },
        { "enchanting", SKILL_ENCHANTING },
        { "ench", SKILL_ENCHANTING },
        { "engineering", SKILL_ENGINEERING },
        { "eng", SKILL_ENGINEERING },
        { "herbalism", SKILL_HERBALISM },
        { "herb", SKILL_HERBALISM },
        { "inscription", SKILL_INSCRIPTION },
        { "insc", SKILL_INSCRIPTION },
        { "jewelcrafting", SKILL_JEWELCRAFTING },
        { "jewel", SKILL_JEWELCRAFTING },
        { "jc", SKILL_JEWELCRAFTING },
        { "leatherworking", SKILL_LEATHERWORKING },
        { "lw", SKILL_LEATHERWORKING },
        { "mining", SKILL_MINING },
        { "mine", SKILL_MINING },
        { "skinning", SKILL_SKINNING },
        { "skin", SKILL_SKINNING },
        { "tailoring", SKILL_TAILORING },
        { "tailor", SKILL_TAILORING },
        { "tail", SKILL_TAILORING }
    };

    return aliases;
}

std::string ProfessionSkillToName(uint16 skillId)
{
    switch (skillId)
    {
        case SKILL_ALCHEMY:
            return "alchemy";
        case SKILL_BLACKSMITHING:
            return "blacksmithing";
        case SKILL_ENCHANTING:
            return "enchanting";
        case SKILL_ENGINEERING:
            return "engineering";
        case SKILL_HERBALISM:
            return "herbalism";
        case SKILL_INSCRIPTION:
            return "inscription";
        case SKILL_JEWELCRAFTING:
            return "jewelcrafting";
        case SKILL_LEATHERWORKING:
            return "leatherworking";
        case SKILL_MINING:
            return "mining";
        case SKILL_SKINNING:
            return "skinning";
        case SKILL_TAILORING:
            return "tailoring";
        default:
            return "unknown";
    }
}

std::string BuildProfessionListMessage()
{
    return "Valid profession skills: alchemy, blacksmithing, enchanting, engineering, herbalism, inscription, jewelcrafting, leatherworking, mining, skinning, tailoring.";
}

bool ResolveProfessionSkill(std::string const& token, uint16& skillId)
{
    std::string const normalized = NormalizeToken(token);
    auto const it = GetProfessionAliases().find(normalized);
    if (it == GetProfessionAliases().end())
        return false;

    skillId = it->second;
    return true;
}

uint16 ComputeLevelSkillCap(Player* player)
{
    if (!player)
        return 1;

    uint32 const cap = uint32(player->GetLevel()) * 5u;
    return static_cast<uint16>(std::clamp<uint32>(cap, 1u, std::numeric_limits<uint16>::max()));
}

uint16 GetSkillStepForValue(uint16 value)
{
    if (value <= 75)
        return 1;
    if (value <= 150)
        return 2;
    if (value <= 225)
        return 3;
    if (value <= 300)
        return 4;
    if (value <= 375)
        return 5;

    return 6;
}

void NormalizeRidingSkillForLevel(Player* target)
{
    if (!target)
        return;

    if (target->GetLevel() < 40)
    {
        if (target->HasSkill(SKILL_RIDING))
            target->SetSkill(SKILL_RIDING, 0, 0, 0);

        return;
    }

    uint16 desiredValue = 75;

    if (target->GetLevel() >= 70)
        desiredValue = 225;

    uint16 const step = GetSkillStepForValue(desiredValue);

    target->SetSkill(SKILL_RIDING, step, desiredValue, desiredValue);
}

void RemoveLevel60EpicClassMountSpellsForSpecPlayer(Player* target)
{
    if (!target || target->GetLevel() != 60)
        return;

    switch (target->getClass())
    {
        case CLASS_PALADIN:
            target->removeSpell(23214, SPEC_MASK_ALL, false);
            target->removeSpell(34767, SPEC_MASK_ALL, false);
            break;
        case CLASS_WARLOCK:
            target->removeSpell(23161, SPEC_MASK_ALL, false);
            break;
        default:
            break;
    }
}

bool ParseRequestedProfessions(Optional<std::string> skill1Arg,
                               Optional<std::string> skill2Arg,
                               ProfessionPair& professions, std::string& errorMessage)
{
    professions = { 0, 0 };

    bool const hasSkill1 = skill1Arg.has_value();
    bool const hasSkill2 = skill2Arg.has_value();
    if (!hasSkill1 && !hasSkill2)
        return true;

    if (!hasSkill1 || !hasSkill2)
    {
        errorMessage = "provide both skill1 and skill2, or omit both.";
        return false;
    }

    uint16 firstSkill = 0;
    uint16 secondSkill = 0;
    if (!ResolveProfessionSkill(*skill1Arg, firstSkill))
    {
        errorMessage = "unknown skill1 '" + *skill1Arg + "'. " + BuildProfessionListMessage();
        return false;
    }

    if (!ResolveProfessionSkill(*skill2Arg, secondSkill))
    {
        errorMessage = "unknown skill2 '" + *skill2Arg + "'. " + BuildProfessionListMessage();
        return false;
    }

    if (firstSkill == secondSkill)
    {
        errorMessage = "skill1 and skill2 must be different professions.";
        return false;
    }

    professions = { firstSkill, secondSkill };
    return true;
}

bool ParseRequestedProfessions(std::string const& skill1Arg, std::string const& skill2Arg,
                               ProfessionPair& professions, std::string& errorMessage)
{
    return ParseRequestedProfessions(Optional<std::string>(skill1Arg), Optional<std::string>(skill2Arg), professions, errorMessage);
}

void ApplyRequestedPrimaryProfessions(Player* target, ProfessionPair professions);
void NormalizeKnownSkillsToLevelCap(Player* target);
void ResetBotAIAndActions(PlayerbotAI* botAI);
bool SetPetTankState(Player* bot, bool enabled);

struct ModuleConfig
{
    bool enabled = true;
    bool requireMasterControl = true;
    bool showSpecListOnEmpty = true;
    bool loginDiagnosticsEnable = true;

    bool autoGearRndBots = true;
    bool autoGearAltBots = false;

    std::string gearModeRndBots = "master_ilvl_ratio";
    std::string gearModeAltBots = "master_ilvl_ratio";
    float gearRatioRndBots = 1.0f;
    float gearRatioAltBots = 1.0f;

    std::string expansionSource = "auto";
    float gearValidationLowerRatio = 0.85f;
    float gearValidationUpperRatio = 1.15f;
    uint8 gearRetryCount = 4;
    uint8 gearQualityCapRatioMode = ITEM_QUALITY_EPIC;
    uint8 gearQualityCapTopForLevel = ITEM_QUALITY_LEGENDARY;

    uint8 specPlayerMinSecurity = SEC_GAMEMASTER;
    bool specPlayerAllowOfflineQueue = true;
    bool specPlayerApplyPrimaryProfessions = true;
    bool specPlayerNormalizeKnownSkills = true;
    bool specPlayerNormalizeRiding = true;
    uint8 specPlayerRidingBasicLevel = 40;
    uint16 specPlayerRidingBasicSkill = 75;
    uint8 specPlayerRidingAdvancedLevel = 70;
    uint16 specPlayerRidingAdvancedSkill = 225;
    bool specPlayerRemoveLevel60EpicClassMountSpells = true;
    bool specPlayerPostGlyphs = true;
    bool specPlayerPostConsumables = true;
    bool specPlayerPostPet = true;
    bool specPlayerPostPetTalents = true;
    uint32 specPlayerTargetAverageIlvl60 = 62;
    uint32 specPlayerTargetAverageIlvl70 = 105;
    uint8 specPlayerGearRetryCount = 6;
    uint8 specPlayerGearQualityCap = ITEM_QUALITY_EPIC;
    bool specPlayerExcludeQuestRewardItems = true;
    bool specPlayerEnforceUniqueRingTrinketPairs = true;
    uint8 specPlayerGearLevelSearchWindow = 10;
};

/* Read module knobs from config and clamp dangerous values before they can
 * stage a dramatic escape from reality.
 */

ModuleConfig LoadModuleConfig()
{
    ModuleConfig config;

    /* First collect raw preferences from config; defaults are the life raft. */

    config.enabled = sConfigMgr->GetOption<bool>(CONF_SPEC_ENABLE, true);
    config.requireMasterControl = sConfigMgr->GetOption<bool>(CONF_REQUIRE_MASTER_CONTROL, true);
    config.showSpecListOnEmpty = sConfigMgr->GetOption<bool>(CONF_SHOW_SPEC_LIST_ON_EMPTY, true);
    config.loginDiagnosticsEnable = sConfigMgr->GetOption<bool>(CONF_LOGIN_DIAGNOSTICS_ENABLE, true);

    config.autoGearRndBots = sConfigMgr->GetOption<bool>(CONF_AUTO_GEAR_RNDBOTS, true);
    config.autoGearAltBots = sConfigMgr->GetOption<bool>(CONF_AUTO_GEAR_ALTBOTS, false);

    config.gearModeRndBots = NormalizeToken(sConfigMgr->GetOption<std::string>(CONF_GEAR_MODE_RNDBOTS, "master_ilvl_ratio"));
    config.gearModeAltBots = NormalizeToken(sConfigMgr->GetOption<std::string>(CONF_GEAR_MODE_ALTBOTS, "master_ilvl_ratio"));
    config.gearRatioRndBots = sConfigMgr->GetOption<float>(CONF_GEAR_RATIO_RNDBOTS, 1.0f);
    config.gearRatioAltBots = sConfigMgr->GetOption<float>(CONF_GEAR_RATIO_ALTBOTS, 1.0f);

    /* Normalize text settings so casing and punctuation do not become policy decisions. */

    config.expansionSource = NormalizeToken(sConfigMgr->GetOption<std::string>(CONF_EXPANSION_SOURCE, "auto"));
    config.gearValidationLowerRatio = sConfigMgr->GetOption<float>(CONF_GEAR_VALIDATION_LOWER_RATIO, 0.85f);
    config.gearValidationUpperRatio = sConfigMgr->GetOption<float>(CONF_GEAR_VALIDATION_UPPER_RATIO, 1.15f);
    config.gearRetryCount = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_GEAR_RETRY_COUNT, 4));
    config.gearQualityCapRatioMode = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_GEAR_QUALITY_CAP_RATIO_MODE, ITEM_QUALITY_EPIC));
    config.gearQualityCapTopForLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_GEAR_QUALITY_CAP_TOP_FOR_LEVEL, ITEM_QUALITY_LEGENDARY));
    config.specPlayerMinSecurity = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_MIN_SECURITY, SEC_GAMEMASTER));
    config.specPlayerAllowOfflineQueue = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_ALLOW_OFFLINE_QUEUE, true);
    config.specPlayerApplyPrimaryProfessions = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_APPLY_PRIMARY_PROFESSIONS, true);
    config.specPlayerNormalizeKnownSkills = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_NORMALIZE_KNOWN_SKILLS, true);
    config.specPlayerNormalizeRiding = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_NORMALIZE_RIDING, true);
    config.specPlayerRidingBasicLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_RIDING_BASIC_LEVEL, 40));
    config.specPlayerRidingBasicSkill = static_cast<uint16>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_RIDING_BASIC_SKILL, 75));
    config.specPlayerRidingAdvancedLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_RIDING_ADVANCED_LEVEL, 70));
    config.specPlayerRidingAdvancedSkill = static_cast<uint16>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_RIDING_ADVANCED_SKILL, 225));
    config.specPlayerRemoveLevel60EpicClassMountSpells =
        sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_REMOVE_LEVEL60_EPIC_CLASS_MOUNT_SPELLS, true);
    config.specPlayerPostGlyphs = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_POST_GLYPHS, true);
    config.specPlayerPostConsumables = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_POST_CONSUMABLES, true);
    config.specPlayerPostPet = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_POST_PET, true);
    config.specPlayerPostPetTalents = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_POST_PET_TALENTS, true);
    config.specPlayerTargetAverageIlvl60 =
        sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_TARGET_AVERAGE_ILVL_60, 62);
    config.specPlayerTargetAverageIlvl70 =
        sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_TARGET_AVERAGE_ILVL_70, 105);
    config.specPlayerGearRetryCount = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_GEAR_RETRY_COUNT, 6));
    config.specPlayerGearQualityCap = static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_GEAR_QUALITY_CAP, ITEM_QUALITY_EPIC));
    config.specPlayerExcludeQuestRewardItems = sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_EXCLUDE_QUEST_REWARD_ITEMS, true);
    config.specPlayerEnforceUniqueRingTrinketPairs =
        sConfigMgr->GetOption<bool>(CONF_SPECPLAYER_ENFORCE_UNIQUE_RING_TRINKET_PAIRS, true);
    config.specPlayerGearLevelSearchWindow =
        static_cast<uint8>(sConfigMgr->GetOption<uint32>(CONF_SPECPLAYER_GEAR_LEVEL_SEARCH_WINDOW, 10));

    /* Clamp ratios; negative item level multipliers are fun in theory and cursed in practice. */

    if (config.gearRatioRndBots < 0.0f)
        config.gearRatioRndBots = 0.0f;

    if (config.gearRatioAltBots < 0.0f)
        config.gearRatioAltBots = 0.0f;

    /* Keep validation and normalization values in a sane range before use. */

    if (config.gearValidationLowerRatio <= 0.0f)
        config.gearValidationLowerRatio = 0.01f;

    if (config.gearValidationUpperRatio < config.gearValidationLowerRatio)
        config.gearValidationUpperRatio = config.gearValidationLowerRatio;

    config.gearRetryCount = std::clamp<uint8>(config.gearRetryCount, 1, 20);
    config.gearQualityCapRatioMode = std::clamp<uint8>(config.gearQualityCapRatioMode, ITEM_QUALITY_NORMAL, ITEM_QUALITY_LEGENDARY);
    config.gearQualityCapTopForLevel = std::clamp<uint8>(config.gearQualityCapTopForLevel, ITEM_QUALITY_NORMAL, ITEM_QUALITY_LEGENDARY);
    config.specPlayerMinSecurity = std::clamp<uint8>(config.specPlayerMinSecurity, SEC_PLAYER, SEC_ADMINISTRATOR);
    config.specPlayerRidingBasicLevel = std::clamp<uint8>(config.specPlayerRidingBasicLevel, 1, std::numeric_limits<uint8>::max());
    config.specPlayerRidingAdvancedLevel = std::clamp<uint8>(config.specPlayerRidingAdvancedLevel, config.specPlayerRidingBasicLevel,
                                                             std::numeric_limits<uint8>::max());
    config.specPlayerRidingBasicSkill = std::clamp<uint16>(config.specPlayerRidingBasicSkill, 1, std::numeric_limits<uint16>::max());
    config.specPlayerRidingAdvancedSkill =
        std::clamp<uint16>(config.specPlayerRidingAdvancedSkill, config.specPlayerRidingBasicSkill, std::numeric_limits<uint16>::max());
    config.specPlayerTargetAverageIlvl60 = std::clamp<uint32>(config.specPlayerTargetAverageIlvl60, 1u, 1000u);
    config.specPlayerTargetAverageIlvl70 = std::clamp<uint32>(config.specPlayerTargetAverageIlvl70, 1u, 1000u);
    config.specPlayerGearRetryCount = std::clamp<uint8>(config.specPlayerGearRetryCount, 1, 20);
    config.specPlayerGearQualityCap = std::clamp<uint8>(config.specPlayerGearQualityCap, ITEM_QUALITY_NORMAL, ITEM_QUALITY_LEGENDARY);
    config.specPlayerGearLevelSearchWindow = std::clamp<uint8>(config.specPlayerGearLevelSearchWindow, 0, 30);

    return config;
}

void NormalizeSpecPlayerRidingForLevel(Player* target, ModuleConfig const& config)
{
    if (!target || !config.specPlayerNormalizeRiding)
        return;

    if (target->GetLevel() < config.specPlayerRidingBasicLevel)
    {
        if (target->HasSkill(SKILL_RIDING))
            target->SetSkill(SKILL_RIDING, 0, 0, 0);

        return;
    }

    uint16 desiredValue = config.specPlayerRidingBasicSkill;

    if (target->GetLevel() >= config.specPlayerRidingAdvancedLevel)
        desiredValue = config.specPlayerRidingAdvancedSkill;

    uint16 const step = GetSkillStepForValue(desiredValue);
    target->SetSkill(SKILL_RIDING, step, desiredValue, desiredValue);
}

struct SpecDefinition
{
    std::string canonical;
    std::vector<std::string> aliases;
    std::vector<std::string> matchTokens;
    std::vector<uint8> preferredSpecIndexes;
};

struct ClassSpecProfile
{
    std::vector<SpecDefinition> specs;
    std::map<std::string, std::vector<std::string>> roles;
};

using ClassSpecMap = std::map<uint8, ClassSpecProfile>;

/* Canonical class->spec dictionary.
 * Aliases are what humans type at 2am; canonical names are what logic can trust.
 * preferredSpecIndexes are first choice; token matching is the backup detective.
 */

ClassSpecMap const& GetClassSpecProfiles()
{
    static ClassSpecMap profiles = {
        {
            CLASS_WARRIOR,
            {
                {
                    { "arms", { "arms", "arm" }, { "arms" }, { 0 } },
                    { "fury", { "fury", "fur" }, { "fury" }, { 1 } },
                    { "protection", { "protection", "prot" }, { "prot", "protection" }, { 2 } },
                },
                {
                    { "tank", { "protection" } },
                    { "melee", { "arms", "fury" } },
                    { "dps", { "arms", "fury" } },
                },
            },
        },
        {
            CLASS_PALADIN,
            {
                {
                    { "holy", { "holy", "hpal" }, { "holy" }, { 0 } },
                    { "protection", { "protection", "prot" }, { "prot", "protection" }, { 1 } },
                    { "retribution", { "retribution", "ret" }, { "ret", "retribution" }, { 2 } },
                },
                {
                    { "tank", { "protection" } },
                    { "heal", { "holy" } },
                    { "melee", { "retribution" } },
                    { "dps", { "retribution" } },
                },
            },
        },
        {
            CLASS_HUNTER,
            {
                {
                    { "beastmaster", { "beastmaster", "beastmastery", "beast mastery", "bm" }, { "bm", "beast" }, { 0 } },
                    { "marksman", { "marksman", "mm" }, { "mm", "marksman", "marksmanship" }, { 1 } },
                    { "survival", { "survival", "surv", "sv" }, { "surv", "survival" }, { 2 } },
                },
                {
                    { "ranged", { "beastmaster", "marksman", "survival" } },
                    { "dps", { "beastmaster", "marksman", "survival" } },
                },
            },
        },
        {
            CLASS_ROGUE,
            {
                {
                    { "assassination", { "assassination", "as" }, { "as", "assassination" }, { 0 } },
                    { "combat", { "combat", "comb" }, { "combat" }, { 1 } },
                    { "subtlety", { "subtlety", "sub" }, { "subtlety", "sub" }, { 2 } },
                },
                {
                    { "melee", { "assassination", "combat", "subtlety" } },
                    { "dps", { "assassination", "combat", "subtlety" } },
                },
            },
        },
        {
            CLASS_PRIEST,
            {
                {
                    { "discipline", { "discipline", "disc" }, { "disc", "discipline" }, { 0 } },
                    { "holy", { "holy", "hpr" }, { "holy" }, { 1 } },
                    { "shadow", { "shadow", "spr" }, { "shadow" }, { 2 } },
                },
                {
                    { "heal", { "discipline", "holy" } },
                    { "ranged", { "shadow" } },
                    { "dps", { "shadow" } },
                },
            },
        },
        {
            CLASS_DEATH_KNIGHT,
            {
                {
                    { "blood_tank", { "blood_tank", "blood tank", "bloodtank", "bdkt" }, { "blood" }, { 0 } },
                    { "blood_dps", { "blood_dps", "blood dps", "blooddps", "bdkd" }, { "double aura blood", "blood dps", "blood" }, { 3, 0 } },
                    { "frost", { "frost", "fr" }, { "frost" }, { 1 } },
                    { "unholy", { "unholy", "uh" }, { "unholy" }, { 2 } },
                },
                {
                    { "tank", { "blood_tank" } },
                    { "melee", { "blood_dps", "frost", "unholy" } },
                    { "dps", { "blood_dps", "frost", "unholy" } },
                },
            },
        },
        {
            CLASS_SHAMAN,
            {
                {
                    { "elemental", { "elemental", "ele" }, { "ele", "elemental" }, { 0 } },
                    { "enhancement", { "enhancement", "enh" }, { "enh", "enhancement" }, { 1 } },
                    { "restoration", { "restoration", "resto" }, { "resto", "restoration" }, { 2 } },
                },
                {
                    { "heal", { "restoration" } },
                    { "melee", { "enhancement" } },
                    { "ranged", { "elemental" } },
                    { "dps", { "elemental", "enhancement" } },
                },
            },
        },
        {
            CLASS_MAGE,
            {
                {
                    { "arcane", { "arcane", "arc" }, { "arcane" }, { 0 } },
                    { "fire", { "fire", "fir" }, { "fire" }, { 1 } },
                    { "frost", { "frost", "fr" }, { "frost" }, { 2 } },
                },
                {
                    { "ranged", { "arcane", "fire", "frost" } },
                    { "dps", { "arcane", "fire", "frost" } },
                },
            },
        },
        {
            CLASS_WARLOCK,
            {
                {
                    { "affliction", { "affliction", "affli", "aff" }, { "affli", "affliction" }, { 0 } },
                    { "demonology", { "demonology", "demo" }, { "demo", "demonology" }, { 1 } },
                    { "destruction", { "destruction", "destro", "dest" }, { "destro", "destruction" }, { 2 } },
                },
                {
                    { "ranged", { "affliction", "demonology", "destruction" } },
                    { "dps", { "affliction", "demonology", "destruction" } },
                },
            },
        },
        {
            CLASS_DRUID,
            {
                {
                    { "balance", { "balance", "bal" }, { "balance" }, { 0 } },
                    { "feral_tank", { "feral_tank", "feral tank", "feraltank", "bear" }, { "bear" }, { 1 } },
                    { "feral_dps", { "feral_dps", "feral dps", "feraldps", "cat" }, { "cat" }, { 3 } },
                    { "restoration", { "restoration", "resto" }, { "resto", "restoration" }, { 2 } },
                },
                {
                    { "tank", { "feral_tank" } },
                    { "heal", { "restoration" } },
                    { "melee", { "feral_dps" } },
                    { "ranged", { "balance" } },
                    { "dps", { "balance", "feral_dps" } },
                },
            },
        },
    };

    return profiles;
}

SpecDefinition const* FindSpecDefinition(ClassSpecProfile const& profile, std::string const& canonical)
{
    auto const it = std::find_if(profile.specs.begin(), profile.specs.end(), [&](SpecDefinition const& spec)
    {
        return spec.canonical == canonical;
    });

    return it != profile.specs.end() ? &(*it) : nullptr;
}

/* Match intent tokens against premade labels.
 * Supports single words and phrases, because humans enjoy both abbreviations and poetry.
 */

bool MatchPremadeNameByToken(std::string const& premadeNameLower, std::vector<std::string> const& tokens)
{
    if (tokens.empty())
        return false;

    /* Build a normalized word list so punctuation cannot outvote intent. */

    std::vector<std::string> words = SplitWords(premadeNameLower);
    std::vector<std::string> normalizedWords;
    normalizedWords.reserve(words.size());
    for (std::string const& word : words)
        normalizedWords.push_back(NormalizeToken(word));

    /* Evaluate each token:
     * - phrases use substring matching,
     * - single words use normalized token matching.
     */

    for (std::string token : tokens)
    {
        token = ToLower(token);

        if (token.find(' ') != std::string::npos)
        {
            if (premadeNameLower.find(token) != std::string::npos)
                return true;

            continue;
        }

        std::string tokenNorm = NormalizeToken(token);
        if (tokenNorm.empty())
            continue;

        if (std::find(normalizedWords.begin(), normalizedWords.end(), tokenNorm) != normalizedWords.end())
            return true;
    }

    return false;
}

/* Resolve a canonical spec definition into the premade template index used by playerbots.
 * Order of preference:
 * 1) explicit preferred indexes that also token-match,
 * 2) first token match with PVE in name,
 * 3) first token match,
 * 4) first available preferred index.
 * In other words: strict when possible, practical when the world is on fire.
 */

int FindSpecNoForDefinition(uint8 classId, SpecDefinition const& spec)
{
    auto const hasPremade = [&](uint8 specNo)
    {
        if (specNo >= MAX_SPECNO)
            return false;

        return !sPlayerbotAIConfig.premadeSpecName[classId][specNo].empty();
    };

    /* Phase 1: trust preferred slots when they exist and the names still match intent. */

    for (uint8 preferred : spec.preferredSpecIndexes)
    {
        if (!hasPremade(preferred))
            continue;

        std::string const premadeName = ToLower(sPlayerbotAIConfig.premadeSpecName[classId][preferred]);
        if (MatchPremadeNameByToken(premadeName, spec.matchTokens))
            return preferred;
    }

    int firstPveMatch = -1;
    int firstAnyMatch = -1;

    /* Phase 2: scan all available templates and remember the best candidates. */

    for (uint8 specNo = 0; specNo < MAX_SPECNO; ++specNo)
    {
        std::string const premadeName = sPlayerbotAIConfig.premadeSpecName[classId][specNo];
        if (premadeName.empty())
            break;

        std::string const premadeNameLower = ToLower(premadeName);
        if (!MatchPremadeNameByToken(premadeNameLower, spec.matchTokens))
            continue;

        if (firstAnyMatch < 0)
            firstAnyMatch = specNo;

        if (firstPveMatch < 0 && premadeNameLower.find("pve") != std::string::npos)
            firstPveMatch = specNo;
    }

    /* Phase 3: resolve in priority order before giving up. */

    if (firstPveMatch >= 0)
        return firstPveMatch;

    if (firstAnyMatch >= 0)
        return firstAnyMatch;

    for (uint8 preferred : spec.preferredSpecIndexes)
    {
        if (hasPremade(preferred))
            return preferred;
    }

    return -1;
}

std::string FormatCanonicalName(std::string const& canonical)
{
    std::string name = canonical;
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

std::string BuildSpecListMessageForClass(uint8 classId)
{
    auto const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(classId);
    if (profileIt == profiles.end())
        return "No spec profile is defined for this class.";

    ClassSpecProfile const& profile = profileIt->second;

    std::ostringstream exact;
    for (size_t i = 0; i < profile.specs.size(); ++i)
    {
        if (i != 0)
            exact << ", ";

        exact << FormatCanonicalName(profile.specs[i].canonical);
    }

    std::ostringstream roles;
    bool firstRole = true;
    for (auto const& [roleName, _] : profile.roles)
    {
        if (!firstRole)
            roles << ", ";

        roles << roleName;
        firstRole = false;
    }

    std::string message = "Available specs: " + exact.str() + '.';
    if (!firstRole)
        message += " Available roles: " + roles.str() + '.';

    return message;
}

std::string BuildSpecListMessage(Player* bot)
{
    if (!bot)
        return "No spec profile is defined for this class.";

    return BuildSpecListMessageForClass(bot->getClass());
}

enum class PetSpecChoice
{
    None,
    Tank,
    Dps,
    Stealth,
    Control,
};

char const* PetSpecChoiceToString(PetSpecChoice choice)
{
    switch (choice)
    {
        case PetSpecChoice::Tank:
            return "tank";
        case PetSpecChoice::Dps:
            return "dps";
        case PetSpecChoice::Stealth:
            return "stealth";
        case PetSpecChoice::Control:
            return "control";
        case PetSpecChoice::None:
        default:
            return "";
    }
}

bool ParsePetSpecChoice(std::string const& token, PetSpecChoice& choice)
{
    std::string const normalized = NormalizeToken(token);
    if (normalized == "tank")
    {
        choice = PetSpecChoice::Tank;
        return true;
    }

    if (normalized == "dps")
    {
        choice = PetSpecChoice::Dps;
        return true;
    }

    if (normalized == "stealth")
    {
        choice = PetSpecChoice::Stealth;
        return true;
    }

    if (normalized == "control")
    {
        choice = PetSpecChoice::Control;
        return true;
    }

    choice = PetSpecChoice::None;
    return false;
}

bool SupportsPetSpecCommand(Player* bot)
{
    return bot && (bot->getClass() == CLASS_HUNTER || bot->getClass() == CLASS_WARLOCK);
}

std::string BuildPetSpecListMessage(Player* bot)
{
    if (!SupportsPetSpecCommand(bot))
        return "petspec is only available for hunters and warlocks. Available pet specs: tank, dps, stealth, control.";

    return "Available pet specs: tank, dps, stealth, control.";
}

enum class BotCommandType
{
    None,
    Setup,
    Spec,
    Restock,
    PetSpec
};

struct ParsedBotCommand
{
    BotCommandType type = BotCommandType::None;
    bool listOnly = false;
    PetSpecChoice petSpecChoice = PetSpecChoice::None;
    std::string specProfile;
    std::string errorMessage;
};

ParsedBotCommand ParseBotCommand(std::string const& command)
{
    ParsedBotCommand parsed;

    std::vector<std::string> words = SplitWords(command);
    if (words.empty())
        return parsed;

    std::string const verb = NormalizeToken(words.front());

    if (verb == "setup")
    {
        parsed.type = BotCommandType::Setup;
        if (words.size() != 1)
            parsed.errorMessage = "setup takes no arguments.";
        return parsed;
    }

    if (verb == "restock")
    {
        parsed.type = BotCommandType::Restock;
        if (words.size() != 1)
            parsed.errorMessage = "restock takes no arguments.";
        return parsed;
    }

    if (verb == "petspec")
    {
        parsed.type = BotCommandType::PetSpec;
        if (words.size() == 1)
        {
            parsed.listOnly = true;
            return parsed;
        }

        if (words.size() != 2 || !ParsePetSpecChoice(words[1], parsed.petSpecChoice))
            parsed.errorMessage = "usage: petspec <tank|dps|stealth|control>.";
        return parsed;
    }

    if (verb != "spec")
        return parsed;

    parsed.type = BotCommandType::Spec;

    if (words.size() == 1)
    {
        parsed.listOnly = true;
        return parsed;
    }

    parsed.specProfile = JoinWords(words, 1);
    return parsed;
}

struct ResolvedSpec
{
    SpecDefinition const* definition = nullptr;
};

bool ResolveRequestedSpec(uint8 classId, std::string const& requestedProfile, ResolvedSpec& resolved, bool allowRoleSelection = true)
{
    auto const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(classId);
    if (profileIt == profiles.end())
        return false;

    ClassSpecProfile const& profile = profileIt->second;
    std::string const requestedNorm = NormalizeToken(requestedProfile);

    /* First attempt exact aliases; deterministic behavior is easier to trust. */

    for (SpecDefinition const& exact : profile.specs)
    {
        for (std::string const& alias : exact.aliases)
        {
            if (requestedNorm != NormalizeToken(alias))
                continue;

            resolved.definition = &exact;
            return true;
        }
    }

    if (!allowRoleSelection)
        return false;

    auto const roleIt = profile.roles.find(requestedNorm);
    if (roleIt == profile.roles.end() || roleIt->second.empty())
        return false;

    std::vector<std::string> const& options = roleIt->second;
    uint32 selectedIndex = options.size() == 1 ? 0 : urand(0, options.size() - 1);
    std::string const& selectedCanonical = options[selectedIndex];

    resolved.definition = FindSpecDefinition(profile, selectedCanonical);
    return resolved.definition != nullptr;
}

bool ResolveRequestedSpec(Player* bot, std::string const& requestedProfile, ResolvedSpec& resolved, bool allowRoleSelection = true)
{
    return bot && ResolveRequestedSpec(bot->getClass(), requestedProfile, resolved, allowRoleSelection);
}

enum class ExpansionCap
{
    Wrath,
    TBC,
    Vanilla,
};

/* Turn expansion enums into stable human words for login diagnostics. */

char const* ExpansionCapToString(ExpansionCap cap)
{
    switch (cap)
    {
        case ExpansionCap::Vanilla:
            return "Vanilla";
        case ExpansionCap::TBC:
            return "TBC";
        case ExpansionCap::Wrath:
        default:
            return "Wrath";
    }
}

uint8 GetExpansionCapOrder(ExpansionCap cap)
{
    switch (cap)
    {
        case ExpansionCap::Vanilla:
            return 0;
        case ExpansionCap::TBC:
            return 1;
        case ExpansionCap::Wrath:
        default:
            return 2;
    }
}

/* Fallback expansion detector: old reliable level bands. */

ExpansionCap GetLevelBasedCap(Player* bot)
{
    if (bot->GetLevel() <= 60)
        return ExpansionCap::Vanilla;

    if (bot->GetLevel() <= 70)
        return ExpansionCap::TBC;

    return ExpansionCap::Wrath;
}

bool TryGetProgressionTierFromSettings(ObjectGuid::LowType guidLow, uint8& tier)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT data FROM character_settings WHERE guid = {} AND source = 'mod-individual-progression' LIMIT 1",
        guidLow);

    if (!result)
        return false;

    std::string const data = (*result)[0].Get<std::string>();
    if (data.empty())
        return false;

    std::stringstream stream(data);
    uint32 value = 0;

    if (!(stream >> value))
        return false;

    tier = static_cast<uint8>(value);
    return true;
}

/* Map mod-individual-progression tiers to expansion buckets. */

ExpansionCap GetProgressionBasedCap(uint8 progressionTier)
{
    if (progressionTier <= 7)
        return ExpansionCap::Vanilla;

    if (progressionTier <= 12)
        return ExpansionCap::TBC;

    return ExpansionCap::Wrath;
}

ExpansionCap ResolveConfiguredExpansionCap(Player* bot, ModuleConfig const& config)
{
    if (!bot)
        return ExpansionCap::Wrath;

    ExpansionCap const levelCap = GetLevelBasedCap(bot);
    std::string const mode = config.expansionSource;

    if (mode == "level")
        return levelCap;

    if (mode == "progression" || mode == "auto")
    {
        uint8 progressionTier = 0;
        if (TryGetProgressionTierFromSettings(bot->GetGUID().GetCounter(), progressionTier))
            return GetProgressionBasedCap(progressionTier);

        return levelCap;
    }

    return levelCap;
}

/* Decide which expansion cap to use for talent filtering.
 * If limitTalentsExpansion is disabled upstream, this always resolves to Wrath.
 * "auto" means target progression tier first, then level if that data is missing.
 */

ExpansionCap ResolveExpansionCap(Player* bot, ModuleConfig const& config)
{
    /* If upstream expansion limiting is disabled, we keep our hands off the dial. */

    if (!sPlayerbotAIConfig.limitTalentsExpansion)
        return ExpansionCap::Wrath;

    return ResolveConfiguredExpansionCap(bot, config);
}

ExpansionCap ResolveSetupExpansionCap(Player* bot, ModuleConfig const& config)
{
    return ResolveConfiguredExpansionCap(bot, config);
}

/* Hard gate for talent nodes when expansion limiting is active.
 * Vanilla allows up to row 6 center node; TBC up to row 8 center node.
 * This prevents helpful commands from inventing time travel.
 */

bool IsAllowedTalentNode(ExpansionCap cap, uint32 row, uint32 col)
{
    if (cap == ExpansionCap::Vanilla)
        return !(row > 6 || (row == 6 && col != 1));

    if (cap == ExpansionCap::TBC)
        return !(row > 8 || (row == 8 && col != 1));

    return true;
}

/* Build the parsed template path beginning from the nearest level that has entries.
 * This mirrors how premade trees are defined incrementally across levels.
 */

std::vector<std::vector<uint32>> BuildTemplatePath(Player* bot, uint8 classId, int specNo)
{
    int startLevel = static_cast<int>(bot->GetLevel());

    /* Step backward to the nearest level with parsed data, then replay forward to 80. */

    while (startLevel > 1 && startLevel < 80 &&
           sPlayerbotAIConfig.parsedSpecLinkOrder[classId][specNo][startLevel].empty())
    {
        --startLevel;
    }

    std::vector<std::vector<uint32>> path;

    for (int level = startLevel; level <= 80; ++level)
    {
        std::vector<std::vector<uint32>> const& entries = sPlayerbotAIConfig.parsedSpecLinkOrder[classId][specNo][level];
        path.insert(path.end(), entries.begin(), entries.end());
    }

    return path;
}

uint32 GetPrimaryTalentTab(std::vector<std::vector<uint32>> const& parsedPath)
{
    std::map<uint32, uint32> pointTotals;

    for (std::vector<uint32> const& entry : parsedPath)
    {
        if (entry.size() < 4)
            continue;

        pointTotals[entry[0]] += entry[3];
    }

    uint32 primaryTab = 0;
    uint32 highestPoints = 0;

    for (auto const& [tab, points] : pointTotals)
    {
        if (points <= highestPoints)
            continue;

        primaryTab = tab;
        highestPoints = points;
    }

    return primaryTab;
}

/* Some caps do not support glyphs; when in doubt, wipe to a clean state. */

void ClearGlyphs(Player* bot)
{
    for (uint32 slotIndex = 0; slotIndex < MAX_GLYPH_SLOT_INDEX; ++slotIndex)
        bot->SetGlyph(slotIndex, 0, true);

    bot->SendTalentsInfoData(false);
}

void FillRemainingTalentsInTree(Player* bot, uint32 specTab, ExpansionCap cap)
{
    if (!bot || bot->GetFreeTalentPoints() == 0)
        return;

    uint32 const classMask = bot->getClassMask();
    std::map<uint32, std::vector<TalentEntry const*>> spellsByRow;

    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo || !IsAllowedTalentNode(cap, talentInfo->Row, talentInfo->Col))
            continue;

        TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo || talentTabInfo->tabpage != specTab)
            continue;

        if ((classMask & talentTabInfo->ClassMask) == 0)
            continue;

        spellsByRow[talentInfo->Row].push_back(talentInfo);
    }

    uint32 freePoints = bot->GetFreeTalentPoints();

    for (auto& rowEntry : spellsByRow)
    {
        std::vector<TalentEntry const*>& spells = rowEntry.second;
        if (spells.empty())
            continue;

        int attemptCount = 0;
        while (!spells.empty() && static_cast<int>(freePoints) - static_cast<int>(bot->GetFreeTalentPoints()) < 5 &&
               attemptCount++ < 3 && bot->GetFreeTalentPoints())
        {
            uint32 const index = urand(0, spells.size() - 1);
            TalentEntry const* talentInfo = spells[index];
            int maxRank = 0;

            for (int rank = 0; rank < std::min<uint32>(MAX_TALENT_RANK, bot->GetFreeTalentPoints()); ++rank)
            {
                if (!talentInfo->RankID[rank])
                    continue;

                maxRank = rank;
            }

            if (talentInfo->DependsOn)
            {
                bot->LearnTalent(talentInfo->DependsOn,
                                 std::min(talentInfo->DependsOnRank, bot->GetFreeTalentPoints() - 1));
            }

            bot->LearnTalent(talentInfo->TalentID, maxRank);
            spells.erase(spells.begin() + index);
        }

        freePoints = bot->GetFreeTalentPoints();
        if (freePoints == 0)
            break;
    }
}

void FillRemainingTalentPoints(Player* bot, std::vector<std::vector<uint32>> const& parsedPath, ExpansionCap cap)
{
    if (!bot || parsedPath.empty() || bot->GetFreeTalentPoints() == 0)
        return;

    uint32 const primaryTab = GetPrimaryTalentTab(parsedPath);
    FillRemainingTalentsInTree(bot, (primaryTab + 1) % 3, cap);

    if (bot->GetFreeTalentPoints())
        FillRemainingTalentsInTree(bot, (primaryTab + 2) % 3, cap);
}

/* Apply talent points from parsed template path, filtered by expansion cap.
 * If parsed data is missing, fallback to the existing specNo initializer.
 */

bool ApplySpecTalents(Player* bot, int specNo, ExpansionCap cap)
{
    std::vector<std::vector<uint32>> parsedPath = BuildTemplatePath(bot, bot->getClass(), specNo);

    /* No parsed path means we fall back to the legacy spec initializer. */

    if (parsedPath.empty())
    {
        PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true);
        return true;
    }

    std::vector<std::vector<uint32>> filtered;
    filtered.reserve(parsedPath.size());

    /* Filter template nodes through the current expansion cap before applying. */

    for (std::vector<uint32> const& entry : parsedPath)
    {
        if (entry.size() < 4)
            continue;

        uint32 row = entry[1];
        uint32 col = entry[2];

        if (!IsAllowedTalentNode(cap, row, col))
            continue;

        filtered.push_back(entry);
    }

    /* If filtering removed everything, fallback prevents a talentless existential crisis. */

    if (filtered.empty())
    {
        PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true);
        return true;
    }

    PlayerbotFactory::InitTalentsByParsedSpecLink(bot, filtered, true);
    FillRemainingTalentPoints(bot, parsedPath, cap);
    bot->SendTalentsInfoData(false);
    return true;
}

uint32 EncodeTalentNodeKey(uint32 tab, uint32 row, uint32 col)
{
    return (tab << 16) | (row << 8) | col;
}

std::unordered_map<uint32, uint32> BuildCurrentTalentRanks(Player* bot)
{
    std::unordered_map<uint32, uint32> ranks;
    if (!bot)
        return ranks;

    PlayerTalentMap const& talentMap = bot->GetTalentMap();
    for (PlayerTalentMap::const_iterator itr = talentMap.begin(); itr != talentMap.end(); ++itr)
    {
        uint32 const spellId = itr->first;
        if ((bot->GetActiveSpecMask() & itr->second->specMask) == 0)
            continue;

        TalentSpellPos const* talentPos = GetTalentSpellPos(spellId);
        if (!talentPos)
            continue;

        TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentPos->talent_id);
        if (!talentInfo)
            continue;

        TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        uint32 const rank = spellInfo ? std::max<uint32>(1, spellInfo->GetRank()) : 1;
        uint32 const key = EncodeTalentNodeKey(talentTabInfo->tabpage, talentInfo->Row, talentInfo->Col);

        auto const [it, inserted] = ranks.try_emplace(key, rank);
        if (!inserted)
            it->second = std::max(it->second, rank);
    }

    return ranks;
}

std::unordered_map<uint32, uint32> BuildTemplateTalentRanks(Player* bot, int specNo)
{
    std::unordered_map<uint32, uint32> ranks;
    if (!bot || specNo < 0)
        return ranks;

    for (std::vector<uint32> const& entry : BuildTemplatePath(bot, bot->getClass(), specNo))
    {
        if (entry.size() < 4)
            continue;

        uint32 const key = EncodeTalentNodeKey(entry[0], entry[1], entry[2]);
        auto const [it, inserted] = ranks.try_emplace(key, entry[3]);
        if (!inserted)
            it->second = std::max(it->second, entry[3]);
    }

    return ranks;
}

int FindBestCurrentSpecNo(Player* bot)
{
    if (!bot)
        return -1;

    ClassSpecMap const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(bot->getClass());
    if (profileIt == profiles.end())
        return -1;

    std::unordered_map<uint32, uint32> const currentRanks = BuildCurrentTalentRanks(bot);
    if (currentRanks.empty())
        return -1;

    int bestSpecNo = -1;
    uint32 bestScore = 0;
    uint32 bestMatchedNodes = 0;

    for (SpecDefinition const& spec : profileIt->second.specs)
    {
        int const specNo = FindSpecNoForDefinition(bot->getClass(), spec);
        if (specNo < 0)
            continue;

        std::unordered_map<uint32, uint32> const templateRanks = BuildTemplateTalentRanks(bot, specNo);
        if (templateRanks.empty())
            continue;

        uint32 score = 0;
        uint32 matchedNodes = 0;

        for (auto const& [nodeKey, currentRank] : currentRanks)
        {
            auto const templateIt = templateRanks.find(nodeKey);
            if (templateIt == templateRanks.end())
                continue;

            ++matchedNodes;
            score += std::min(currentRank, templateIt->second);
            if (currentRank == templateIt->second)
                ++score;
        }

        if (score > bestScore || (score == bestScore && matchedNodes > bestMatchedNodes))
        {
            bestSpecNo = specNo;
            bestScore = score;
            bestMatchedNodes = matchedNodes;
        }
    }

    return bestScore == 0 ? -1 : bestSpecNo;
}

SpecDefinition const* FindSpecDefinitionForSpecNo(Player* bot, int specNo)
{
    if (!bot || specNo < 0)
        return nullptr;

    ClassSpecMap const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(bot->getClass());
    if (profileIt == profiles.end())
        return nullptr;

    for (SpecDefinition const& spec : profileIt->second.specs)
    {
        if (FindSpecNoForDefinition(bot->getClass(), spec) == specNo)
            return &spec;
    }

    return nullptr;
}

bool ResolveCurrentSpec(Player* bot, ResolvedSpec& resolved)
{
    if (!bot)
        return false;

    std::string const aiSpecName = AiFactory::GetPlayerSpecName(bot);
    if (!aiSpecName.empty() && ResolveRequestedSpec(bot, aiSpecName, resolved, true) && resolved.definition)
        return true;

    int const specNo = FindBestCurrentSpecNo(bot);
    resolved.definition = FindSpecDefinitionForSpecNo(bot, specNo);
    return resolved.definition != nullptr;
}

bool IsProtectionPaladinSpec(SpecDefinition const* definition)
{
    return definition && NormalizeToken(definition->canonical) == "protection";
}

void NormalizePaladinRighteousFury(Player* bot, PlayerbotAI* botAI, SpecDefinition const* definition)
{
    if (!bot || bot->getClass() != CLASS_PALADIN || IsProtectionPaladinSpec(definition))
        return;

    bot->RemoveAurasDueToSpell(SPELL_RIGHTEOUS_FURY);
    bot->RemoveAurasDueToSpell(SPELL_RIGHTEOUS_FURY_THREAT_PASSIVE);

    if (!botAI)
        return;

    if (botAI->HasStrategy("bthreat", BOT_STATE_COMBAT))
        botAI->ChangeStrategy("-bthreat", BOT_STATE_COMBAT);

    if (botAI->HasStrategy("bthreat", BOT_STATE_NON_COMBAT))
        botAI->ChangeStrategy("-bthreat", BOT_STATE_NON_COMBAT);
}

void ReapplySetupTalentsForCap(Player* bot, ExpansionCap cap)
{
    if (!bot || bot->GetLevel() < 10)
        return;

    int const specNo = FindBestCurrentSpecNo(bot);
    if (specNo < 0)
        return;

    ApplySpecTalents(bot, specNo, cap);
}

Optional<PetSpecChoice> LoadSavedPetSpec(Player* bot)
{
    if (!bot)
        return {};

    QueryResult result = CharacterDatabase.Query(
        "SELECT data FROM character_settings WHERE guid = {} AND source = '{}' LIMIT 1",
        bot->GetGUID().GetCounter(), PET_SPEC_SOURCE);

    if (!result)
        return {};

    PetSpecChoice choice = PetSpecChoice::None;
    if (!ParsePetSpecChoice((*result)[0].Get<std::string>(), choice))
        return {};

    return choice;
}

void SavePetSpec(Player* bot, PetSpecChoice choice)
{
    if (!bot)
        return;

    if (choice == PetSpecChoice::None)
    {
        CharacterDatabase.Execute(
            "DELETE FROM character_settings WHERE guid = {} AND source = '{}'",
            bot->GetGUID().GetCounter(), PET_SPEC_SOURCE);
        return;
    }

    CharacterDatabase.Execute(
        "REPLACE INTO character_settings (guid, source, data) VALUES ({}, '{}', '{}')",
        bot->GetGUID().GetCounter(), PET_SPEC_SOURCE, PetSpecChoiceToString(choice));
}

constexpr int32 HUNTER_PET_TALENT_FEROCITY = 0;
constexpr int32 HUNTER_PET_TALENT_TENACITY = 1;
constexpr int32 HUNTER_PET_TALENT_CUNNING = 2;

struct WarlockPetChoice
{
    char const* strategy = "";
    char const* summonAction = "";
    uint32 summonSpellId = 0;
    uint32 npcEntry = 0;
    bool tauntEnabled = false;
};

bool GetWarlockPetDefinition(std::string const& strategy, WarlockPetChoice& choice)
{
    if (strategy == "imp")
    {
        choice = { "imp", "summon imp", 688, 416, false };
        return true;
    }

    if (strategy == "voidwalker")
    {
        choice = { "voidwalker", "summon voidwalker", 697, 1860, true };
        return true;
    }

    if (strategy == "succubus")
    {
        choice = { "succubus", "summon succubus", 712, 1863, false };
        return true;
    }

    if (strategy == "felhunter")
    {
        choice = { "felhunter", "summon felhunter", 691, 417, false };
        return true;
    }

    if (strategy == "felguard")
    {
        choice = { "felguard", "summon felguard", 30146, 17252, true };
        return true;
    }

    return false;
}

std::vector<std::string> BuildWarlockPetStrategies(SpecDefinition const* specDefinition, PetSpecChoice petSpecChoice)
{
    if (!specDefinition)
        return {};

    switch (petSpecChoice)
    {
        case PetSpecChoice::Tank:
            return { "felguard", "voidwalker" };
        case PetSpecChoice::Dps:
            if (specDefinition->canonical == "demonology")
                return { "felguard", "imp" };
            if (specDefinition->canonical == "affliction")
                return { "succubus", "imp" };
            return { "imp" };
        case PetSpecChoice::Stealth:
            if (specDefinition->canonical == "destruction")
                return { "imp" };
            return { "succubus", "imp" };
        case PetSpecChoice::Control:
            return { "felhunter", "succubus", "imp" };
        case PetSpecChoice::None:
        default:
            return {};
    }
}

bool ResolveWarlockPetChoice(Player* bot, SpecDefinition const* specDefinition, PetSpecChoice petSpecChoice, WarlockPetChoice& choice)
{
    if (!bot || !specDefinition)
        return false;

    std::vector<std::string> const strategies = BuildWarlockPetStrategies(specDefinition, petSpecChoice);
    if (strategies.empty())
        return false;

    Pet* currentPet = bot->GetPet();
    WarlockPetChoice currentChoice;
    size_t currentChoiceIndex = strategies.size();
    if (currentPet)
    {
        for (size_t i = 0; i < strategies.size(); ++i)
        {
            WarlockPetChoice candidate;
            if (!GetWarlockPetDefinition(strategies[i], candidate))
                continue;

            if (currentPet->GetEntry() != candidate.npcEntry)
                continue;

            currentChoice = candidate;
            currentChoiceIndex = i;
            break;
        }
    }

    for (size_t i = 0; i < currentChoiceIndex; ++i)
    {
        WarlockPetChoice candidate;
        if (!GetWarlockPetDefinition(strategies[i], candidate))
            continue;

        if (!bot->HasSpell(candidate.summonSpellId))
            continue;

        choice = candidate;
        return true;
    }

    if (currentChoiceIndex < strategies.size())
    {
        choice = currentChoice;
        return true;
    }

    for (size_t i = 0; i < strategies.size(); ++i)
    {
        WarlockPetChoice candidate;
        if (!GetWarlockPetDefinition(strategies[i], candidate))
            continue;

        if (!bot->HasSpell(candidate.summonSpellId))
            continue;

        choice = candidate;
        return true;
    }

    return false;
}

std::array<char const*, 5> const& GetWarlockPetStrategies()
{
    static std::array<char const*, 5> strategies = {
        "imp",
        "voidwalker",
        "succubus",
        "felhunter",
        "felguard"
    };

    return strategies;
}

void ApplyWarlockPetStrategy(PlayerbotAI* botAI, char const* desiredStrategy)
{
    if (!botAI || !desiredStrategy || !*desiredStrategy)
        return;

    for (char const* strategy : GetWarlockPetStrategies())
    {
        if (std::string(strategy) == desiredStrategy)
            continue;

        if (botAI->HasStrategy(strategy, BOT_STATE_NON_COMBAT))
            botAI->ChangeStrategy(std::string("-") + strategy, BOT_STATE_NON_COMBAT);
    }

    if (!botAI->HasStrategy(desiredStrategy, BOT_STATE_NON_COMBAT))
        botAI->ChangeStrategy(std::string("+") + desiredStrategy, BOT_STATE_NON_COMBAT);
}

bool EnsureWarlockPet(Player* bot, PlayerbotAI* botAI, WarlockPetChoice const& choice)
{
    if (!bot || !botAI || !choice.summonSpellId || !choice.summonAction || !*choice.summonAction)
        return false;

    Pet* pet = bot->GetPet();
    if (pet && pet->GetEntry() == choice.npcEntry)
        return true;

    if (!bot->HasSpell(choice.summonSpellId))
        return false;

    if (!botAI->DoSpecificAction(choice.summonAction, Event(), true))
        return false;

    pet = bot->GetPet();
    return pet && pet->GetEntry() == choice.npcEntry;
}

bool ConfigureWarlockPetSpec(Player* bot, PlayerbotAI* botAI, SpecDefinition const* specDefinition,
                             PetSpecChoice petSpecChoice, std::string& errorMessage)
{
    if (!bot || !botAI || bot->getClass() != CLASS_WARLOCK || !specDefinition)
    {
        errorMessage = "petspec is only available for hunters and warlocks.";
        return false;
    }

    WarlockPetChoice choice;
    if (!ResolveWarlockPetChoice(bot, specDefinition, petSpecChoice, choice))
    {
        errorMessage = "no suitable demon is available for petspec " + std::string(PetSpecChoiceToString(petSpecChoice)) +
                       " on " + bot->GetName() + '.';
        return false;
    }

    ApplyWarlockPetStrategy(botAI, choice.strategy);
    if (!EnsureWarlockPet(bot, botAI, choice))
    {
        errorMessage = "failed to summon the requested demon for " + bot->GetName() + '.';
        return false;
    }

    Pet* pet = bot->GetPet();
    if (!pet || pet->GetEntry() != choice.npcEntry)
    {
        errorMessage = "failed to activate the requested demon for " + bot->GetName() + '.';
        return false;
    }

    if (choice.tauntEnabled && !SetPetTankState(bot, true))
    {
        errorMessage = "failed to enable demon taunt autocast for " + bot->GetName() + '.';
        return false;
    }

    if (!choice.tauntEnabled)
        SetPetTankState(bot, false);

    return true;
}

struct HunterPetChoice
{
    std::vector<uint32> preferredFamilies;
    int32 petTalentType = -1;
    bool tauntEnabled = false;
    bool allowSimilarFamilies = false;
};

enum class HunterOwnedPetLocation
{
    None,
    Current,
    Stable,
    Unslotted,
};

struct HunterOwnedPetChoice
{
    uint32 petNumber = 0;
    HunterOwnedPetLocation location = HunterOwnedPetLocation::None;
    size_t stableSlot = 0;
};

HunterPetChoice GetHunterPetChoice(PetSpecChoice petSpecChoice)
{
    switch (petSpecChoice)
    {
        case PetSpecChoice::Tank:
            return { { CREATURE_FAMILY_BEAR, CREATURE_FAMILY_TURTLE, CREATURE_FAMILY_BOAR }, HUNTER_PET_TALENT_TENACITY, true, true };
        case PetSpecChoice::Dps:
            return { { CREATURE_FAMILY_WOLF }, HUNTER_PET_TALENT_FEROCITY, false, false };
        case PetSpecChoice::Stealth:
            return { { CREATURE_FAMILY_CAT }, HUNTER_PET_TALENT_FEROCITY, false, false };
        case PetSpecChoice::Control:
            return { { CREATURE_FAMILY_BIRD_OF_PREY }, HUNTER_PET_TALENT_CUNNING, false, true };
        case PetSpecChoice::None:
        default:
            return {};
    }
}

int32 GetHunterPetTalentType(uint32 family)
{
    CreatureFamilyEntry const* petFamily = sCreatureFamilyStore.LookupEntry(family);
    return petFamily ? petFamily->petTalentType : -1;
}

bool IsHunterPetFamilyAllowedForChoice(uint32 family, HunterPetChoice const& choice, bool preferredOnly)
{
    if (preferredOnly)
        return std::find(choice.preferredFamilies.begin(), choice.preferredFamilies.end(), family) != choice.preferredFamilies.end();

    return choice.petTalentType >= 0 && GetHunterPetTalentType(family) == choice.petTalentType;
}

bool DoesCurrentHunterPetMatchChoice(Player* bot, HunterPetChoice const& choice)
{
    Pet* pet = bot ? bot->GetPet() : nullptr;
    if (!pet)
        return false;

    CreatureTemplate const* creature = pet->GetCreatureTemplate();
    if (!creature)
        return false;

    if (std::find(choice.preferredFamilies.begin(), choice.preferredFamilies.end(), creature->family) != choice.preferredFamilies.end())
        return true;

    return choice.allowSimilarFamilies && choice.petTalentType >= 0 && GetHunterPetTalentType(creature->family) == choice.petTalentType;
}

bool TryGetHunterPetFamilyFromEntry(uint32 creatureEntry, uint32& family)
{
    CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(creatureEntry);
    if (!creature)
        return false;

    family = creature->family;
    return family != 0;
}

bool DoesOwnedHunterPetMatchChoice(PetStable::PetInfo const& petInfo, HunterPetChoice const& choice, bool preferredOnly)
{
    if (petInfo.Type != HUNTER_PET)
        return false;

    uint32 family = 0;
    if (!TryGetHunterPetFamilyFromEntry(petInfo.CreatureId, family))
        return false;

    return IsHunterPetFamilyAllowedForChoice(family, choice, preferredOnly);
}

bool FindOwnedHunterPetForChoice(Player* bot, HunterPetChoice const& choice, bool preferredOnly, bool allowUnslotted,
                                 HunterOwnedPetChoice& ownedChoice)
{
    PetStable const* petStable = bot ? bot->GetPetStable() : nullptr;
    if (!petStable)
        return false;

    if (petStable->CurrentPet && DoesOwnedHunterPetMatchChoice(petStable->CurrentPet.value(), choice, preferredOnly))
    {
        ownedChoice = { petStable->CurrentPet->PetNumber, HunterOwnedPetLocation::Current, 0 };
        return true;
    }

    for (size_t slot = 0; slot < petStable->StabledPets.size(); ++slot)
    {
        Optional<PetStable::PetInfo> const& stabledPet = petStable->StabledPets[slot];
        if (!stabledPet || !DoesOwnedHunterPetMatchChoice(stabledPet.value(), choice, preferredOnly))
            continue;

        ownedChoice = { stabledPet->PetNumber, HunterOwnedPetLocation::Stable, slot };
        return true;
    }

    if (!allowUnslotted)
        return false;

    for (PetStable::PetInfo const& unslottedPet : petStable->UnslottedPets)
    {
        if (!DoesOwnedHunterPetMatchChoice(unslottedPet, choice, preferredOnly))
            continue;

        ownedChoice = { unslottedPet.PetNumber, HunterOwnedPetLocation::Unslotted, 0 };
        return true;
    }

    return false;
}

bool FindAnyOwnedHunterPet(Player* bot, bool allowUnslotted, HunterOwnedPetChoice& ownedChoice)
{
    PetStable const* petStable = bot ? bot->GetPetStable() : nullptr;
    if (!petStable)
        return false;

    if (petStable->CurrentPet && petStable->CurrentPet->Type == HUNTER_PET)
    {
        ownedChoice = { petStable->CurrentPet->PetNumber, HunterOwnedPetLocation::Current, 0 };
        return true;
    }

    for (size_t slot = 0; slot < petStable->StabledPets.size(); ++slot)
    {
        Optional<PetStable::PetInfo> const& stabledPet = petStable->StabledPets[slot];
        if (!stabledPet || stabledPet->Type != HUNTER_PET)
            continue;

        ownedChoice = { stabledPet->PetNumber, HunterOwnedPetLocation::Stable, slot };
        return true;
    }

    if (!allowUnslotted)
        return false;

    for (PetStable::PetInfo const& unslottedPet : petStable->UnslottedPets)
    {
        if (unslottedPet.Type != HUNTER_PET)
            continue;

        ownedChoice = { unslottedPet.PetNumber, HunterOwnedPetLocation::Unslotted, 0 };
        return true;
    }

    return false;
}

Pet* LoadOwnedHunterPet(Player* bot, HunterOwnedPetChoice const& ownedChoice)
{
    if (!bot || bot->getClass() != CLASS_HUNTER || ownedChoice.location == HunterOwnedPetLocation::None)
        return nullptr;

    if (ownedChoice.location == HunterOwnedPetLocation::Current)
    {
        Pet* currentPet = bot->GetPet();
        PetStable const* petStable = bot->GetPetStable();
        if (currentPet && petStable && petStable->CurrentPet && petStable->CurrentPet->PetNumber == ownedChoice.petNumber)
            return currentPet;
    }

    Pet* oldPet = bot->GetPet();
    if (oldPet)
    {
        if (!oldPet->IsAlive() || !oldPet->IsHunterPet() || ownedChoice.location != HunterOwnedPetLocation::Stable)
            return nullptr;

        bot->RemovePet(oldPet, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + ownedChoice.stableSlot));
    }

    Pet* pet = new Pet(bot, HUNTER_PET);
    if (!pet->LoadPetFromDB(bot, 0, ownedChoice.petNumber, false))
    {
        delete pet;
        return nullptr;
    }

    return pet;
}

bool ApplyHunterPetChoice(Player* bot, HunterPetChoice const& choice, std::string& errorMessage)
{
    if (!bot || !bot->GetPet())
    {
        errorMessage = "no active hunter pet.";
        return false;
    }

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitPetTalents();

    if (choice.tauntEnabled && !SetPetTankState(bot, true))
    {
        errorMessage = "failed to enable pet taunt autocast for " + bot->GetName() + '.';
        return false;
    }

    if (!choice.tauntEnabled)
        SetPetTankState(bot, false);

    return true;
}

std::vector<uint32> CollectHunterPetTemplateIds(Player* bot, HunterPetChoice const& choice, bool preferredOnly)
{
    std::vector<uint32> ids;
    if (!bot)
        return ids;

    CreatureTemplateContainer const* creatures = sObjectMgr->GetCreatureTemplates();
    if (!creatures)
        return ids;

    for (CreatureTemplateContainer::const_iterator itr = creatures->begin(); itr != creatures->end(); ++itr)
    {
        CreatureTemplate const& creature = itr->second;
        if (!creature.IsTameable(bot->CanTameExoticPets()))
            continue;

        if (creature.minlevel > bot->GetLevel())
            continue;

        if (creature.Name.size() > 21)
            continue;

        if (std::find(sPlayerbotAIConfig.excludedHunterPetFamilies.begin(),
                      sPlayerbotAIConfig.excludedHunterPetFamilies.end(),
                      creature.family) != sPlayerbotAIConfig.excludedHunterPetFamilies.end())
            continue;

        if (!IsHunterPetFamilyAllowedForChoice(creature.family, choice, preferredOnly))
            continue;

        ids.push_back(itr->first);
    }

    return ids;
}

void ClearHunterPetState(Player* bot)
{
    if (!bot)
        return;

    if (bot->GetPetStable() && bot->GetPetStable()->CurrentPet)
    {
        bot->RemovePet(nullptr, PET_SAVE_AS_CURRENT);
        bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);
    }

    if (bot->GetPetStable() && bot->GetPetStable()->GetUnslottedHunterPet())
    {
        bot->GetPetStable()->UnslottedPets.clear();
        bot->RemovePet(nullptr, PET_SAVE_AS_CURRENT);
        bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);
    }
}

Pet* CreateHunterPetByEntry(Player* bot, uint32 creatureEntry)
{
    if (!bot || bot->getClass() != CLASS_HUNTER || bot->GetLevel() < 10 || !bot->GetMap())
        return nullptr;

    ClearHunterPetState(bot);

    Pet* pet = bot->CreateTamedPetFrom(creatureEntry, 0);
    if (!pet)
        return nullptr;

    pet->SetUInt32Value(UNIT_FIELD_LEVEL, bot->GetLevel() - 1);
    pet->GetMap()->AddToMap(pet->ToCreature());
    pet->SetUInt32Value(UNIT_FIELD_LEVEL, bot->GetLevel());

    bot->SetMinion(pet, true);
    pet->InitTalentForLevel();
    pet->SavePetToDB(PET_SAVE_AS_CURRENT);
    bot->PetSpellInitialize();

    pet->InitStatsForLevel(bot->GetLevel());
    pet->SetLevel(bot->GetLevel());
    pet->SetPower(POWER_HAPPINESS, pet->GetMaxPower(Powers(POWER_HAPPINESS)));
    pet->SetHealth(pet->GetMaxHealth());

    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
    {
        if (itr->second.state == PETSPELL_REMOVED)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);
        if (!spellInfo || spellInfo->IsPassive())
            continue;

        pet->ToggleAutocast(spellInfo, true);
    }

    return pet;
}

bool ConfigureHunterPetSpec(Player* bot, PetSpecChoice petSpecChoice, bool restrictToOwnedPets, std::string& errorMessage)
{
    if (!bot || bot->getClass() != CLASS_HUNTER)
    {
        errorMessage = "petspec is only available for hunters and warlocks.";
        return false;
    }

    if (bot->GetLevel() < 10)
    {
        errorMessage = bot->GetName() + " cannot use petspec before level 10.";
        return false;
    }

    HunterPetChoice const choice = GetHunterPetChoice(petSpecChoice);
    if (choice.petTalentType < 0)
    {
        errorMessage = "usage: petspec <tank|dps|stealth|control>.";
        return false;
    }

    if (DoesCurrentHunterPetMatchChoice(bot, choice))
        return ApplyHunterPetChoice(bot, choice, errorMessage);

    if (restrictToOwnedPets)
    {
        bool const allowUnslotted = !bot->GetPet();
        HunterOwnedPetChoice ownedChoice;

        if (!FindOwnedHunterPetForChoice(bot, choice, true, allowUnslotted, ownedChoice))
            FindOwnedHunterPetForChoice(bot, choice, false, allowUnslotted, ownedChoice);

        if (ownedChoice.location != HunterOwnedPetLocation::None)
        {
            if (!LoadOwnedHunterPet(bot, ownedChoice))
            {
                errorMessage = "failed to load an owned hunter pet for " + bot->GetName() + '.';
                return false;
            }

            return ApplyHunterPetChoice(bot, choice, errorMessage);
        }

        if (bot->GetPet())
            return ApplyHunterPetChoice(bot, choice, errorMessage);

        if (FindAnyOwnedHunterPet(bot, true, ownedChoice))
        {
            if (!LoadOwnedHunterPet(bot, ownedChoice))
            {
                errorMessage = "failed to load an owned hunter pet for " + bot->GetName() + '.';
                return false;
            }

            return ApplyHunterPetChoice(bot, choice, errorMessage);
        }

        errorMessage = "no owned hunter pet is available for " + bot->GetName() + '.';
        return false;
    }

    std::vector<uint32> ids = CollectHunterPetTemplateIds(bot, choice, true);
    if (ids.empty())
        ids = CollectHunterPetTemplateIds(bot, choice, false);

    if (ids.empty())
    {
        errorMessage = "no suitable hunter pet family is available for " + bot->GetName() + '.';
        return false;
    }

    Pet* pet = nullptr;
    while (!ids.empty() && !pet)
    {
        uint32 const index = ids.size() == 1 ? 0 : urand(0, ids.size() - 1);
        pet = CreateHunterPetByEntry(bot, ids[index]);
        ids.erase(ids.begin() + index);
    }

    if (!pet)
    {
        errorMessage = "failed to create the requested hunter pet for " + bot->GetName() + '.';
        return false;
    }

    return ApplyHunterPetChoice(bot, choice, errorMessage);
}

/* Rndbots and addclass bots share the same policy bucket. */

bool IsRndOrAddclassBot(Player* bot)
{
    return sRandomPlayerbotMgr.IsRandomBot(bot) || sRandomPlayerbotMgr.IsAddclassBot(bot);
}

bool IsAddclassBot(Player* bot)
{
    return sRandomPlayerbotMgr.IsAddclassBot(bot);
}

/* Keep addclass bots in lockstep with the command sender's level before specing. */

void SyncAddclassBotLevel(Player* bot, Player* commandSender)
{
    if (!bot || !commandSender || !IsAddclassBot(bot))
        return;

    uint8 const targetLevel = commandSender->GetLevel();
    if (bot->GetLevel() == targetLevel)
        return;

    bot->CombatStop(true);
    bot->GiveLevel(targetLevel);
    bot->SetUInt32Value(PLAYER_XP, 0);
    bot->InitStatsForLevel(true);
}

/* Gear policy:
 * - rnd/addclass: config toggle controls automatic gearing.
 * - altbots: only gear when command asks for it ("gear") and config allows it.
 * This keeps random bots fast to configure and altbots intentionally opt-in.
 */

bool ShouldAutoGear(Player* bot, bool gearRequested, ModuleConfig const& config)
{
    if (IsRndOrAddclassBot(bot))
        return config.autoGearRndBots;

    return config.autoGearAltBots && gearRequested;
}

/* Compute the module target average ilvl from master average * ratio.
 * This is intentionally average-ilvl based, because that is the user-facing contract.
 */

float ComputeMasterTargetAverageIlvl(Player* commandSender, float ratio)
{
    if (!commandSender)
        return 0.0f;

    float const averageIlvl = commandSender->GetAverageItemLevelForDF();
    if (averageIlvl <= 0.0f)
        return 0.0f;

    float const scaled = averageIlvl * ratio;
    if (scaled <= 0.0f)
        return 0.0f;

    return std::max(1.0f, scaled);
}

/* Convert target average ilvl into the mixed-gear-score cap used by the factory filter. */

uint32 ComputeGearScoreLimitFromAverageIlvl(float targetAverageIlvl)
{
    if (targetAverageIlvl <= 0.0f)
        return 0;

    uint32 const limit = PlayerbotFactory::CalcMixedGearScore(static_cast<uint32>(std::round(targetAverageIlvl)), ITEM_QUALITY_EPIC);
    return limit == 0 ? 1 : limit;
}

/* Gear pre-step:
 * clear ammo so ranged selection can be refreshed,
 * but do not destroy item objects here (that can poison inventory update queues).
 */

void DestroyOldGear(Player* bot)
{
    if (!bot)
        return;

    bot->SetAmmo(0);
}

/* Validate that equipped gear is reasonably close to target average ilvl and
 * does not include low-quality junk.
 */

bool IsGearWithinTargetBand(Player* bot, float targetAverageIlvl, ModuleConfig const& config)
{
    if (!bot || targetAverageIlvl <= 0.0f)
        return true;

    float const lowerBound = std::max(1.0f, targetAverageIlvl * config.gearValidationLowerRatio);
    float const upperBound = targetAverageIlvl * config.gearValidationUpperRatio;

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;

        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;

        if (proto->Quality <= ITEM_QUALITY_NORMAL)
            return false;

        float const itemLevel = static_cast<float>(proto->ItemLevel);
        if (itemLevel < lowerBound || itemLevel > upperBound)
            return false;
    }

    return true;
}

bool IsItemLevelWithinTargetBand(uint32 itemLevel, float targetAverageIlvl, ModuleConfig const& config)
{
    if (targetAverageIlvl <= 0.0f)
        return true;

    float const lowerBound = std::max(1.0f, targetAverageIlvl * config.gearValidationLowerRatio);
    float const upperBound = targetAverageIlvl * config.gearValidationUpperRatio;
    float const itemLevelFloat = static_cast<float>(itemLevel);

    return itemLevelFloat >= lowerBound && itemLevelFloat <= upperBound;
}

uint8 GetPairedRingOrTrinketSlot(uint8 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_FINGER1:
            return EQUIPMENT_SLOT_FINGER2;
        case EQUIPMENT_SLOT_FINGER2:
            return EQUIPMENT_SLOT_FINGER1;
        case EQUIPMENT_SLOT_TRINKET1:
            return EQUIPMENT_SLOT_TRINKET2;
        case EQUIPMENT_SLOT_TRINKET2:
            return EQUIPMENT_SLOT_TRINKET1;
        default:
            return NULL_SLOT;
    }
}

bool IsQuestRewardGearItem(uint32 itemId)
{
    if (!itemId)
        return false;

    static std::unordered_set<uint32> const questRewardItemIds = []()
    {
        std::unordered_set<uint32> ids;
        ObjectMgr::QuestMap const& questTemplates = sObjectMgr->GetQuestTemplates();

        for (ObjectMgr::QuestMap::const_iterator itr = questTemplates.begin(); itr != questTemplates.end(); ++itr)
        {
            Quest const* quest = itr->second;
            if (!quest)
                continue;

            for (uint32 i = 0; i < quest->GetRewItemsCount(); ++i)
                if (quest->RewardItemId[i])
                    ids.insert(quest->RewardItemId[i]);

            for (uint32 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
                if (quest->RewardChoiceItemId[i])
                    ids.insert(quest->RewardChoiceItemId[i]);
        }

        return ids;
    }();

    return questRewardItemIds.find(itemId) != questRewardItemIds.end();
}

bool IsUniqueTwinSlotItem(ItemTemplate const* proto)
{
    return proto && (proto->MaxCount == 1 || proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE) || proto->ItemLimitCategory != 0);
}

bool IsValidGearItemForTargetBand(ItemTemplate const* proto, float targetAverageIlvl, ModuleConfig const& config)
{
    if (!proto || proto->Quality <= ITEM_QUALITY_NORMAL)
        return false;

    return IsItemLevelWithinTargetBand(proto->ItemLevel, targetAverageIlvl, config);
}

bool ViolatesSpecPlayerTwinSlotRule(Player* bot, uint8 slot, ItemTemplate const* proto)
{
    if (!bot || !proto)
        return false;

    uint8 const pairedSlot = GetPairedRingOrTrinketSlot(slot);
    if (pairedSlot == NULL_SLOT)
        return false;

    Item* pairedItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, pairedSlot);
    if (!pairedItem)
        return false;

    ItemTemplate const* pairedProto = pairedItem->GetTemplate();
    if (!pairedProto)
        return false;

    if (proto->ItemLimitCategory != 0 && proto->ItemLimitCategory == pairedProto->ItemLimitCategory)
        return true;

    if (proto->ItemId != pairedProto->ItemId)
        return false;

    return IsUniqueTwinSlotItem(proto) || IsUniqueTwinSlotItem(pairedProto);
}

bool IsValidSpecPlayerGearItem(Player* bot, uint8 slot, ItemTemplate const* proto, float targetAverageIlvl, ModuleConfig const& config)
{
    if (!IsValidGearItemForTargetBand(proto, targetAverageIlvl, config))
        return false;

    if (config.specPlayerExcludeQuestRewardItems && IsQuestRewardGearItem(proto->ItemId))
        return false;

    if (config.specPlayerEnforceUniqueRingTrinketPairs && ViolatesSpecPlayerTwinSlotRule(bot, slot, proto))
        return false;

    return true;
}

bool IsValidTargetBandGearItem(Player* bot, uint8 slot, ItemTemplate const* proto, float targetAverageIlvl, ModuleConfig const& config,
                               bool applySpecPlayerRestrictions)
{
    if (applySpecPlayerRestrictions)
        return IsValidSpecPlayerGearItem(bot, slot, proto, targetAverageIlvl, config);

    return IsValidGearItemForTargetBand(proto, targetAverageIlvl, config);
}

bool IsSpecPlayerGearWithinTargetBand(Player* bot, float targetAverageIlvl, ModuleConfig const& config)
{
    if (!bot || targetAverageIlvl <= 0.0f)
        return true;

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;

        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        if (!IsValidTargetBandGearItem(bot, slot, item->GetTemplate(), targetAverageIlvl, config, true))
            return false;
    }

    return true;
}

bool IsPrimaryArmorSlot(uint8 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
        case EQUIPMENT_SLOT_SHOULDERS:
        case EQUIPMENT_SLOT_CHEST:
        case EQUIPMENT_SLOT_WAIST:
        case EQUIPMENT_SLOT_LEGS:
        case EQUIPMENT_SLOT_FEET:
        case EQUIPMENT_SLOT_WRISTS:
        case EQUIPMENT_SLOT_HANDS:
            return true;
        default:
            return false;
    }
}

bool IsTierArmorSubClass(uint32 subClass)
{
    return subClass == ITEM_SUBCLASS_ARMOR_PLATE || subClass == ITEM_SUBCLASS_ARMOR_MAIL ||
           subClass == ITEM_SUBCLASS_ARMOR_LEATHER || subClass == ITEM_SUBCLASS_ARMOR_CLOTH;
}

uint32 GetPreferredArmorSubClass(Player* bot)
{
    if (bot->HasSkill(SKILL_PLATE_MAIL))
        return ITEM_SUBCLASS_ARMOR_PLATE;

    if (bot->HasSkill(SKILL_MAIL))
        return ITEM_SUBCLASS_ARMOR_MAIL;

    if (bot->HasSkill(SKILL_LEATHER))
        return ITEM_SUBCLASS_ARMOR_LEATHER;

    return ITEM_SUBCLASS_ARMOR_CLOTH;
}

std::vector<InventoryType> GetArmorInventoryTypesForSlot(uint8 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
            return { INVTYPE_HEAD };
        case EQUIPMENT_SLOT_SHOULDERS:
            return { INVTYPE_SHOULDERS };
        case EQUIPMENT_SLOT_CHEST:
            return { INVTYPE_CHEST, INVTYPE_ROBE };
        case EQUIPMENT_SLOT_WAIST:
            return { INVTYPE_WAIST };
        case EQUIPMENT_SLOT_LEGS:
            return { INVTYPE_LEGS };
        case EQUIPMENT_SLOT_FEET:
            return { INVTYPE_FEET };
        case EQUIPMENT_SLOT_WRISTS:
            return { INVTYPE_WRISTS };
        case EQUIPMENT_SLOT_HANDS:
            return { INVTYPE_HANDS };
        default:
            return {};
    }
}

std::vector<InventoryType> GetInventoryTypesForSlot(uint8 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
            return { INVTYPE_HEAD };
        case EQUIPMENT_SLOT_NECK:
            return { INVTYPE_NECK };
        case EQUIPMENT_SLOT_SHOULDERS:
            return { INVTYPE_SHOULDERS };
        case EQUIPMENT_SLOT_BODY:
            return { INVTYPE_BODY };
        case EQUIPMENT_SLOT_CHEST:
            return { INVTYPE_CHEST, INVTYPE_ROBE };
        case EQUIPMENT_SLOT_WAIST:
            return { INVTYPE_WAIST };
        case EQUIPMENT_SLOT_LEGS:
            return { INVTYPE_LEGS };
        case EQUIPMENT_SLOT_FEET:
            return { INVTYPE_FEET };
        case EQUIPMENT_SLOT_WRISTS:
            return { INVTYPE_WRISTS };
        case EQUIPMENT_SLOT_HANDS:
            return { INVTYPE_HANDS };
        case EQUIPMENT_SLOT_FINGER1:
        case EQUIPMENT_SLOT_FINGER2:
            return { INVTYPE_FINGER };
        case EQUIPMENT_SLOT_TRINKET1:
        case EQUIPMENT_SLOT_TRINKET2:
            return { INVTYPE_TRINKET };
        case EQUIPMENT_SLOT_BACK:
            return { INVTYPE_CLOAK };
        case EQUIPMENT_SLOT_MAINHAND:
            return { INVTYPE_WEAPON, INVTYPE_2HWEAPON, INVTYPE_WEAPONMAINHAND };
        case EQUIPMENT_SLOT_OFFHAND:
            return { INVTYPE_WEAPON, INVTYPE_2HWEAPON, INVTYPE_WEAPONOFFHAND, INVTYPE_SHIELD, INVTYPE_HOLDABLE };
        case EQUIPMENT_SLOT_RANGED:
            return { INVTYPE_RANGED, INVTYPE_RANGEDRIGHT, INVTYPE_RELIC };
        default:
            return {};
    }
}

bool PassesExpansionLimitFilter(Player* bot, uint32 itemId)
{
    if (!sPlayerbotAIConfig.limitGearExpansion)
        return true;

    if (bot->GetLevel() <= 60 && itemId >= 23728)
        return false;

    if (bot->GetLevel() <= 70 && itemId >= 35570 && itemId != 36737 && itemId != 37739 && itemId != 37740)
        return false;

    return true;
}

bool CanEquipUnseenItemForModule(Player* bot, uint8 slot, uint16& dest, uint32 itemId)
{
    dest = 0;

    if (Item* testItem = Item::CreateItem(itemId, 1, bot, false, 0, true))
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        InventoryResult result = botAI ? botAI->CanEquipItem(slot, dest, testItem, true, true)
                                       : bot->CanEquipItem(slot, dest, testItem, true, true);
        testItem->RemoveFromUpdateQueueOf(bot);
        delete testItem;
        return result == EQUIP_ERR_OK;
    }

    return false;
}

void EquipPreferredArmorForSlot(Player* bot, StatsWeightCalculator& calculator, uint8 slot, uint32 preferredSubClass,
                                uint32 gearScoreLimit, uint32 qualityLimit, float targetAverageIlvl,
                                ModuleConfig const* config, bool applySpecPlayerRestrictions = false, uint8 levelSearchWindow = 10)
{
    std::vector<InventoryType> const inventoryTypes = GetArmorInventoryTypesForSlot(slot);
    if (inventoryTypes.empty())
        return;

    int32 const level = static_cast<int32>(bot->GetLevel());
    int32 const minLevel = std::max(level - std::min(level, static_cast<int32>(levelSearchWindow)), 1);

    float bestScore = -1.0f;
    uint32 bestItemId = 0;
    uint16 bestDest = 0;

    for (int32 requiredLevel = level; requiredLevel >= minLevel; --requiredLevel)
    {
        for (InventoryType inventoryType : inventoryTypes)
        {
            for (uint32 itemId : sRandomItemMgr.GetCachedEquipments(requiredLevel, inventoryType))
            {
                if (!PassesExpansionLimitFilter(bot, itemId))
                    continue;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto)
                    continue;

                if (proto->Class != ITEM_CLASS_ARMOR || proto->SubClass != preferredSubClass)
                    continue;

                if (!IsTierArmorSubClass(proto->SubClass))
                    continue;

                if (proto->Quality > qualityLimit)
                    continue;

                if (proto->RequiredLevel > bot->GetLevel() || proto->Duration != 0 || proto->Bonding == BIND_QUEST_ITEM)
                    continue;

                if (config && !IsValidTargetBandGearItem(bot, slot, proto, targetAverageIlvl, *config, applySpecPlayerRestrictions))
                    continue;

                uint16 dest = 0;
                if (!CanEquipUnseenItemForModule(bot, slot, dest, itemId))
                    continue;

                float const score = calculator.CalculateItem(itemId);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestItemId = itemId;
                    bestDest = dest;
                }
            }
        }
    }

    if (bestItemId == 0)
        return;

    if (Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

    if (bot->EquipNewItem(bestDest, bestItemId, true))
        bot->AutoUnequipOffhandIfNeed();
}

/* Enforce strict armor tier preference for core armor slots.
 * Priority is highest wearable tier: plate > mail > leather > cloth.
 */

void EnforcePreferredArmorTier(Player* bot, uint32 gearScoreLimit, uint32 qualityLimit)
{
    if (!bot)
        return;

    uint32 const preferredSubClass = GetPreferredArmorSubClass(bot);
    StatsWeightCalculator calculator(bot);

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (!IsPrimaryArmorSlot(slot))
            continue;

        bool needsPreferred = false;

        if (Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            ItemTemplate const* proto = equipped->GetTemplate();
            if (proto && proto->Class == ITEM_CLASS_ARMOR && IsTierArmorSubClass(proto->SubClass) &&
                proto->SubClass != preferredSubClass)
            {
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                needsPreferred = true;
            }
        }
        else
        {
            needsPreferred = true;
        }

        if (needsPreferred)
            EquipPreferredArmorForSlot(bot, calculator, slot, preferredSubClass, gearScoreLimit, qualityLimit, 0.0f, nullptr);
    }
}

void EnforceTargetItemLevelBand(Player* bot, uint32 gearScoreLimit, uint32 qualityLimit, float targetAverageIlvl,
                                ModuleConfig const& config, bool applySpecPlayerRestrictions = false, uint8 levelSearchWindow = 10)
{
    if (!bot || targetAverageIlvl <= 0.0f)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel(), qualityLimit, gearScoreLimit);
    StatsWeightCalculator calculator(bot);
    uint32 const preferredArmorSubClass = GetPreferredArmorSubClass(bot);
    std::vector<uint8> const initSlotsOrder = {
        EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2, EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND,
        EQUIPMENT_SLOT_RANGED, EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_SHOULDERS, EQUIPMENT_SLOT_CHEST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_HANDS, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
        EQUIPMENT_SLOT_BACK
    };

    int32 const level = static_cast<int32>(bot->GetLevel());
    int32 const minLevel = std::max(level - std::min(level, static_cast<int32>(levelSearchWindow)), 1);

    for (uint8 slot : initSlotsOrder)
    {
        Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (equipped)
        {
            ItemTemplate const* equippedProto = equipped->GetTemplate();
            if (equippedProto &&
                IsValidTargetBandGearItem(bot, slot, equippedProto, targetAverageIlvl, config, applySpecPlayerRestrictions))
            {
                continue;
            }
        }

        float bestScore = -1.0f;
        uint32 bestItemId = 0;
        uint16 bestDest = 0;

        for (int32 requiredLevel = level; requiredLevel >= minLevel; --requiredLevel)
        {
            for (InventoryType inventoryType : GetInventoryTypesForSlot(slot))
            {
                for (uint32 itemId : sRandomItemMgr.GetCachedEquipments(requiredLevel, inventoryType))
                {
                    if (!PassesExpansionLimitFilter(bot, itemId))
                        continue;

                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                    if (!proto)
                        continue;

                    if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
                        continue;

                    if (proto->Quality <= ITEM_QUALITY_NORMAL || proto->Quality > qualityLimit)
                        continue;

                    if (!IsValidTargetBandGearItem(bot, slot, proto, targetAverageIlvl, config, applySpecPlayerRestrictions))
                        continue;

                    if (proto->RequiredLevel > bot->GetLevel() || proto->Duration != 0 || proto->Bonding == BIND_QUEST_ITEM)
                        continue;

                    if (slot == EQUIPMENT_SLOT_OFFHAND && bot->getClass() == CLASS_ROGUE &&
                        proto->Class != ITEM_CLASS_WEAPON)
                        continue;

                    if (IsPrimaryArmorSlot(slot))
                    {
                        if (proto->Class != ITEM_CLASS_ARMOR || !IsTierArmorSubClass(proto->SubClass) ||
                            proto->SubClass != preferredArmorSubClass)
                        {
                            continue;
                        }
                    }

                    uint16 dest = 0;
                    if (!CanEquipUnseenItemForModule(bot, slot, dest, itemId))
                        continue;

                    float const score = calculator.CalculateItem(itemId);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestItemId = itemId;
                        bestDest = dest;
                    }
                }
            }
        }

        if (bestItemId == 0)
            continue;

        if (equipped)
            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

        if (bot->EquipNewItem(bestDest, bestItemId, true))
            bot->AutoUnequipOffhandIfNeed();
    }

    EnforcePreferredArmorTier(bot, gearScoreLimit, qualityLimit);
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (!IsPrimaryArmorSlot(slot))
            continue;

        Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!equipped)
            continue;

        ItemTemplate const* proto = equipped->GetTemplate();
        if (!proto || IsValidTargetBandGearItem(bot, slot, proto, targetAverageIlvl, config, applySpecPlayerRestrictions))
            continue;

        EquipPreferredArmorForSlot(bot, calculator, slot, preferredArmorSubClass, gearScoreLimit, qualityLimit,
                                   targetAverageIlvl, &config, applySpecPlayerRestrictions, levelSearchWindow);
    }
}

/* Remove spare gear generated during rerolls from backpack/bags.
 * Keeps equipped items intact and prevents random armor clutter.
 */

void CleanupBagGear(Player* bot)
{
    if (!bot)
        return;

    struct SlotPos
    {
        uint8 bag;
        uint8 slot;
    };

    std::vector<SlotPos> toDestroy;

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (proto && (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON))
                toDestroy.push_back({ INVENTORY_SLOT_BAG_0, slot });
        }
    }

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot));
        if (!bag)
            continue;

        for (uint8 slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            if (Item* item = bag->GetItemByPos(slot))
            {
                ItemTemplate const* proto = item->GetTemplate();
                if (proto && (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON))
                    toDestroy.push_back({ bagSlot, slot });
            }
        }
    }

    for (SlotPos const& pos : toDestroy)
        bot->DestroyItem(pos.bag, pos.slot, true);
}

/* One gearing pass (equipment/ammo/enchants/repair) with a chosen cap.
 * cap == 0 means top-for-level mode.
 */

void RunGearPass(Player* bot, uint32 gearScoreLimit, uint32 qualityLimit)
{
    PlayerbotFactory factory(bot, bot->GetLevel(), qualityLimit, gearScoreLimit);
    factory.InitEquipment(false, false);
    EnforcePreferredArmorTier(bot, gearScoreLimit, qualityLimit);
    factory.InitAmmo();

    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();

    CleanupBagGear(bot);
    bot->DurabilityRepairAll(false, 1.0f, false);
}

void RunGearPass(Player* bot, uint32 gearScoreLimit, uint32 qualityLimit, float targetAverageIlvl, ModuleConfig const& config)
{
    RunGearPass(bot, gearScoreLimit, qualityLimit);
    EnforceTargetItemLevelBand(bot, gearScoreLimit, qualityLimit, targetAverageIlvl, config);
    PlayerbotFactory(bot, bot->GetLevel(), qualityLimit, gearScoreLimit).InitAmmo();
}

void RunGearPass(Player* bot, uint32 gearScoreLimit, uint32 qualityLimit, float targetAverageIlvl, ModuleConfig const& config,
                 bool applySpecPlayerRestrictions, uint8 levelSearchWindow)
{
    RunGearPass(bot, gearScoreLimit, qualityLimit);
    EnforceTargetItemLevelBand(bot, gearScoreLimit, qualityLimit, targetAverageIlvl, config, applySpecPlayerRestrictions,
                               levelSearchWindow);
    PlayerbotFactory(bot, bot->GetLevel(), qualityLimit, gearScoreLimit).InitAmmo();
}

/* Build a readable target-ilvl label from mode/ratio policy.
 * Ratio mode prints a numeric target when possible; otherwise it reports fallback.
 */

std::string BuildTargetIlvlLabel(Player* commandSender, std::string const& mode, float ratio)
{
    bool const useMasterRatio = (mode == "masterilvlratio" || mode == "master_ilvl_ratio");
    if (!useMasterRatio)
        return "top_for_level";

    float const targetAverageIlvl = ComputeMasterTargetAverageIlvl(commandSender, ratio);
    if (targetAverageIlvl <= 0.0f)
        return "top_for_level (ratio fallback)";

    return std::to_string(static_cast<uint32>(targetAverageIlvl));
}

/* Perform equipment generation and post-processing (ammo, enchants, repairs).
 * Ratio mode follows init=auto style cap source; fallback remains top-for-level.
 */

void ApplyAutoGear(Player* bot, Player* commandSender, ModuleConfig const& config)
{
    /* Pick policy bucket first: rnd/addclass and altbots have different agreements. */

    bool const rndbot = IsRndOrAddclassBot(bot);
    std::string mode = rndbot ? config.gearModeRndBots : config.gearModeAltBots;
    float const ratio = rndbot ? config.gearRatioRndBots : config.gearRatioAltBots;

    bool const useMasterRatio = (mode == "masterilvlratio" || mode == "master_ilvl_ratio");

    /* Ratio mode uses init=auto-style cap source (master mixed gs). */

    if (useMasterRatio)
    {
        float const targetAverageIlvl = ComputeMasterTargetAverageIlvl(commandSender, ratio);
        uint32 const gearScoreLimit = ComputeGearScoreLimitFromAverageIlvl(targetAverageIlvl);

        if (targetAverageIlvl > 0.0f && gearScoreLimit != 0)
        {
            for (uint8 attempt = 0; attempt < config.gearRetryCount; ++attempt)
            {
                DestroyOldGear(bot);
                RunGearPass(bot, gearScoreLimit, config.gearQualityCapRatioMode, targetAverageIlvl, config);

                if (IsGearWithinTargetBand(bot, targetAverageIlvl, config))
                    break;
            }

            return;
        }
    }

    /* Top-for-level fallback path for invalid ratio context or explicit top_for_level mode. */

    DestroyOldGear(bot);
    RunGearPass(bot, 0, config.gearQualityCapTopForLevel);
}

/* Maintenance pass after talents:
 * glyphs, consumables, and pet init/talents.
 */

void RunPostSpecMaintenance(Player* bot, ExpansionCap cap)
{
    PlayerbotFactory factory(bot, bot->GetLevel());

    /* Glyph handling depends on cap; non-wrath caps get a clean slate. */

    if (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion)
        factory.InitGlyphs(false);
    else
        ClearGlyphs(bot);

    factory.InitConsumables();
    factory.InitPet();

    /* Pet talents are expansion-gated, same as glyph expectations. */

    if (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion)
        factory.InitPetTalents();
}

void RunSpecPlayerPostSpecMaintenance(Player* bot, ExpansionCap cap, ModuleConfig const& config)
{
    if (!bot)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());

    if (config.specPlayerPostGlyphs)
    {
        if (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion)
            factory.InitGlyphs(false);
        else
            ClearGlyphs(bot);
    }

    if (config.specPlayerPostConsumables)
        factory.InitConsumables();

    if (config.specPlayerPostPet)
        factory.InitPet();

    if (config.specPlayerPostPetTalents && (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion))
        factory.InitPetTalents();
}

bool IsTalentLockedQuestReward(Player* bot, Quest const* quest)
{
    if (!bot || !quest)
        return false;

    int32 const spellId = quest->GetRewSpellCast();
    if (!spellId)
        return false;

    SpellInfo const* rewardSpell = sSpellMgr->GetSpellInfo(spellId);
    if (!rewardSpell)
        return false;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (rewardSpell->Effects[i].Effect != SPELL_EFFECT_LEARN_SPELL || !rewardSpell->Effects[i].TriggerSpell)
            continue;

        uint32 firstRank = sSpellMgr->GetFirstSpellInChain(rewardSpell->Effects[i].TriggerSpell);
        if (!firstRank)
            firstRank = rewardSpell->Effects[i].TriggerSpell;

        bool const talentDependent = GetTalentSpellCost(firstRank) > 0 || sSpellMgr->IsAdditionalTalentSpell(firstRank);
        if (talentDependent && !bot->HasSpell(rewardSpell->Effects[i].TriggerSpell))
            return true;
    }

    return false;
}

/* Learn class quest reward spells by reusing the same Player quest-reward learner. */

void LearnQuestClassSpells(Player* bot)
{
    if (!bot)
        return;

    ObjectMgr::QuestMap const& questTemplates = sObjectMgr->GetQuestTemplates();
    for (ObjectMgr::QuestMap::const_iterator itr = questTemplates.begin(); itr != questTemplates.end(); ++itr)
    {
        Quest const* quest = itr->second;
        if (!quest || !quest->GetRequiredClasses())
            continue;

        if (quest->IsRepeatable() || quest->GetMinLevel() < 10 || quest->GetMinLevel() > bot->GetLevel())
            continue;

        if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false) || !bot->SatisfyQuestSkill(quest, false))
            continue;

        if (IsTalentLockedQuestReward(bot, quest))
            continue;

        bot->learnQuestRewardedSpells(quest);
    }
}

uint32 GetProfessionSkillLineFromSpell(uint32 spellId)
{
    if (!spellId)
        return 0;

    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        uint32 const skillLine = itr->second->SkillLine;
        if (IsPrimaryProfessionSkillId(skillLine) || IsSecondaryProfessionSkillId(skillLine))
            return skillLine;
    }

    return 0;
}

uint32 ResolveTrainerSpellSkillLine(Trainer::Trainer* trainer, Trainer::Spell const* trainerSpell)
{
    if (!trainerSpell)
        return 0;

    if (trainerSpell->ReqSkillLine &&
        (IsPrimaryProfessionSkillId(trainerSpell->ReqSkillLine) || IsSecondaryProfessionSkillId(trainerSpell->ReqSkillLine)))
    {
        return trainerSpell->ReqSkillLine;
    }

    SpellInfo const* trainerSpellInfo = sSpellMgr->GetSpellInfo(trainerSpell->SpellId);
    if (trainerSpellInfo)
    {
        for (SpellEffectInfo const& effect : trainerSpellInfo->GetEffects())
        {
            if (!effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) || !effect.TriggerSpell)
                continue;

            uint32 const skillLine = GetProfessionSkillLineFromSpell(effect.TriggerSpell);
            if (skillLine)
                return skillLine;
        }
    }

    uint32 const spellSkillLine = GetProfessionSkillLineFromSpell(trainerSpell->SpellId);
    if (spellSkillLine)
        return spellSkillLine;

    return trainer ? GetProfessionSkillLineFromSpell(trainer->GetTrainerRequirement()) : 0;
}

bool ShouldTeachTrainerSpell(Player* bot, Trainer::Trainer* trainer, Trainer::Spell const* trainerSpell,
                             bool allowPrimaryProfessionSpells)
{
    if (!bot || !trainer || !trainerSpell || !trainer->CanTeachSpell(bot, trainerSpell))
        return false;

    if (trainer->GetTrainerType() == Trainer::Type::Class)
        return true;

    if (trainer->GetTrainerType() != Trainer::Type::Tradeskill)
        return false;

    uint32 const skillLine = ResolveTrainerSpellSkillLine(trainer, trainerSpell);
    if (IsSecondaryProfessionSkillId(skillLine))
        return true;

    return allowPrimaryProfessionSpells && IsPrimaryProfessionSkillId(skillLine);
}

std::vector<uint32> const& GetTrainerIdsForClass(uint8 classId)
{
    static std::unordered_map<uint8, std::vector<uint32>> trainerIdsByClass;
    auto [it, inserted] = trainerIdsByClass.try_emplace(classId);
    std::vector<uint32>& trainerIds = it->second;

    if (!inserted)
        return trainerIds;

    CreatureTemplateContainer const* creatureTemplateContainer = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = creatureTemplateContainer->begin();
         itr != creatureTemplateContainer->end(); ++itr)
    {
        Trainer::Trainer* trainer = sObjectMgr->GetTrainer(itr->first);
        if (!trainer)
            continue;

        if (trainer->GetTrainerType() != Trainer::Type::Tradeskill && trainer->GetTrainerType() != Trainer::Type::Class)
            continue;

        if (trainer->GetTrainerType() == Trainer::Type::Class)
            trainerIds.push_back(itr->first);
        else
            trainerIds.push_back(itr->first);
    }

    return trainerIds;
}

void InitAvailableSpellsFiltered(Player* bot, bool allowPrimaryProfessionSpells)
{
    if (!bot)
        return;

    for (uint32 trainerId : GetTrainerIdsForClass(bot->getClass()))
    {
        Trainer::Trainer* trainer = sObjectMgr->GetTrainer(trainerId);
        if (!trainer)
            continue;

        if (trainer->GetTrainerType() == Trainer::Type::Class && !trainer->IsTrainerValidForPlayer(bot))
            continue;

        for (auto const& spell : trainer->GetSpells())
        {
            Trainer::Spell const* trainerSpell = trainer->GetSpell(spell.SpellId);
            if (!ShouldTeachTrainerSpell(bot, trainer, trainerSpell, allowPrimaryProfessionSpells))
                continue;

            if (trainerSpell->IsCastable())
                bot->CastSpell(bot, trainerSpell->SpellId, true);
            else
                bot->learnSpell(trainerSpell->SpellId, false);
        }
    }
}

void LearnSecondaryProfessionRanks(Player* bot, uint16 skillId, uint16 targetMaxSkill)
{
    if (!bot)
        return;

    for (SecondaryProfessionRankSpell const& rankSpell : GetSecondaryProfessionRankSpells(skillId))
    {
        if (targetMaxSkill < rankSpell.requiredMaxSkill || bot->HasSpell(rankSpell.spellId))
            continue;

        bot->learnSpell(rankSpell.spellId, false);
    }
}

uint16 GetSecondaryProfessionExpansionCap(ExpansionCap cap)
{
    switch (cap)
    {
        case ExpansionCap::Vanilla:
            return 300;
        case ExpansionCap::TBC:
            return 375;
        case ExpansionCap::Wrath:
        default:
            return 450;
    }
}

void GrantSecondaryProfessions(Player* bot, ExpansionCap cap)
{
    if (!bot)
        return;

    uint16 const skillCap = std::min<uint16>(ComputeLevelSkillCap(bot), GetSecondaryProfessionExpansionCap(cap));

    for (uint16 skillId : GetSecondaryProfessionSkillIds())
    {
        uint16 const currentValue = bot->GetPureSkillValue(skillId);
        uint16 const currentMaxValue = bot->GetPureMaxSkillValue(skillId);
        uint16 const targetValue = std::max(currentValue, skillCap);
        uint16 const targetMaxValue = std::max(currentMaxValue, targetValue);
        uint16 const step = GetSkillStepForValue(targetMaxValue);

        bot->SetSkill(skillId, step, targetValue, targetMaxValue);
        LearnSecondaryProfessionRanks(bot, skillId, targetMaxValue);
    }
}

RidingStateSnapshot CaptureRidingState(Player* bot)
{
    RidingStateSnapshot snapshot;
    if (!bot)
        return snapshot;

    snapshot.step = bot->GetSkillStep(SKILL_RIDING);
    snapshot.value = bot->GetPureSkillValue(SKILL_RIDING);
    snapshot.maxValue = bot->GetPureMaxSkillValue(SKILL_RIDING);
    snapshot.hadSkill = bot->HasSkill(SKILL_RIDING) || snapshot.value != 0 || snapshot.maxValue != 0;

    auto const& ridingRankSpellIds = GetRidingRankSpellIds();
    for (size_t i = 0; i < ridingRankSpellIds.size(); ++i)
        snapshot.knownRankSpells[i] = bot->HasSpell(ridingRankSpellIds[i]);

    return snapshot;
}

void RestoreRidingState(Player* bot, RidingStateSnapshot const& snapshot)
{
    if (!bot)
        return;

    auto const& ridingRankSpellIds = GetRidingRankSpellIds();
    for (size_t i = 0; i < ridingRankSpellIds.size(); ++i)
    {
        if (!snapshot.knownRankSpells[i] && bot->HasSpell(ridingRankSpellIds[i]))
            bot->removeSpell(ridingRankSpellIds[i], SPEC_MASK_ALL, false);
    }

    for (size_t i = 0; i < ridingRankSpellIds.size(); ++i)
    {
        if (snapshot.knownRankSpells[i] && !bot->HasSpell(ridingRankSpellIds[i]))
            bot->learnSpell(ridingRankSpellIds[i], false);
    }

    if (!snapshot.hadSkill)
    {
        if (bot->HasSkill(SKILL_RIDING) || bot->GetPureSkillValue(SKILL_RIDING) != 0 || bot->GetPureMaxSkillValue(SKILL_RIDING) != 0)
            bot->SetSkill(SKILL_RIDING, 0, 0, 0);

        return;
    }

    uint16 const restoredMaxValue = std::max(snapshot.maxValue, snapshot.value);
    uint16 const restoredStep = snapshot.step ? snapshot.step : GetSkillStepForValue(restoredMaxValue);
    bot->SetSkill(SKILL_RIDING, restoredStep, snapshot.value, restoredMaxValue);
}

bool IsPetTauntSpell(SpellInfo const* spellInfo)
{
    return spellInfo && (spellInfo->HasAura(SPELL_AURA_MOD_TAUNT) || spellInfo->HasEffect(SPELL_EFFECT_ATTACK_ME));
}

bool SetPetTankState(Player* bot, bool enabled)
{
    Pet* pet = bot ? bot->GetPet() : nullptr;
    if (!pet)
        return false;

    bool foundTauntSpell = false;

    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
    {
        if (itr->second.state == PETSPELL_REMOVED)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);
        if (!spellInfo || !spellInfo->IsAutocastable() || !IsPetTauntSpell(spellInfo))
            continue;

        foundTauntSpell = true;
        bool const isAutoCast = std::find(pet->m_autospells.begin(), pet->m_autospells.end(), itr->first) != pet->m_autospells.end();
        if (isAutoCast != enabled)
            pet->ToggleAutocast(spellInfo, enabled);
    }

    return foundTauntSpell;
}

/* Spell pass aligned with existing playerbot setup flows. */

void LearnSpellsForCurrentLevel(Player* bot)
{
    if (!bot)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitClassSpells();
    factory.InitAvailableSpells();
    LearnQuestClassSpells(bot);
    factory.InitSpecialSpells();
}

void LearnBotSpellsForCurrentLevel(Player* bot)
{
    if (!bot)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitClassSpells();
    InitAvailableSpellsFiltered(bot, false);
    LearnQuestClassSpells(bot);
    factory.InitSpecialSpells();
}

void ApplyGlyphStateForCap(Player* bot, ExpansionCap cap)
{
    if (!bot)
        return;

    PlayerbotFactory factory(bot, bot->GetLevel());
    if (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion)
        factory.InitGlyphs(false);
    else
        ClearGlyphs(bot);
}

void ApplyClassBotGearAgainstMaster(Player* bot, Player* commandSender, ModuleConfig const& config)
{
    if (!bot || !commandSender || !IsAddclassBot(bot))
        return;

    bool const useMasterRatio = (config.gearModeRndBots == "masterilvlratio" || config.gearModeRndBots == "master_ilvl_ratio");
    float const targetAverageIlvl = useMasterRatio ? ComputeMasterTargetAverageIlvl(commandSender, config.gearRatioRndBots) : 0.0f;
    uint32 const gearScoreLimit = ComputeGearScoreLimitFromAverageIlvl(targetAverageIlvl);

    if (useMasterRatio && targetAverageIlvl > 0.0f && gearScoreLimit != 0)
    {
        for (uint8 attempt = 0; attempt < config.gearRetryCount; ++attempt)
        {
            DestroyOldGear(bot);
            RunGearPass(bot, gearScoreLimit, config.gearQualityCapRatioMode, targetAverageIlvl, config);

            if (IsGearWithinTargetBand(bot, targetAverageIlvl, config))
                return;
        }
    }

    DestroyOldGear(bot);
    RunGearPass(bot, 0, config.gearQualityCapTopForLevel);
}

bool ExecuteSetupCommand(Player* commandSender, Player* bot, PlayerbotAI* botAI, ModuleConfig const& config,
                         std::string& errorMessage)
{
    if (!bot || !botAI)
    {
        errorMessage = "bot AI is not available.";
        return false;
    }

    SyncAddclassBotLevel(bot, commandSender);

    PlayerbotFactory factory(bot, bot->GetLevel());
    bool const isAltBot = botAI->IsAlt();
    auto const shouldRun = [isAltBot](bool altGate) { return !isAltBot || altGate; };
    ExpansionCap const setupCap = ResolveSetupExpansionCap(bot, config);
    Optional<PetSpecChoice> const savedPetSpec = LoadSavedPetSpec(bot);
    bool const useSavedHunterPetSpec = bot->getClass() == CLASS_HUNTER && savedPetSpec && bot->GetLevel() >= 10;

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceAttunementQs))
        factory.InitAttunementQuests();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceBags))
        factory.InitBags(false);

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceAmmo))
        factory.InitAmmo();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceFood))
        factory.InitFood();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceReagents))
        factory.InitReagents();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceConsumables))
        factory.InitConsumables();

    if (shouldRun(sPlayerbotAIConfig.altMaintenancePotions))
        factory.InitPotions();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceTalentTree))
    {
        factory.InitTalentsTree(true);
        ReapplySetupTalentsForCap(bot, setupCap);
    }

    if (shouldRun(sPlayerbotAIConfig.altMaintenancePet) && !useSavedHunterPetSpec)
        factory.InitPet();

    if (shouldRun(sPlayerbotAIConfig.altMaintenancePetTalents) && !useSavedHunterPetSpec)
        factory.InitPetTalents();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceSkills))
    {
        Optional<RidingStateSnapshot> ridingSnapshot;
        if (isAltBot)
            ridingSnapshot = CaptureRidingState(bot);

        factory.InitSkills();
        if (ridingSnapshot)
            RestoreRidingState(bot, ridingSnapshot.value());

        GrantSecondaryProfessions(bot, setupCap);
    }

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceClassSpells))
        factory.InitClassSpells();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceAvailableSpells))
        InitAvailableSpellsFiltered(bot, false);

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceReputation))
        factory.InitReputation();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceSpecialSpells))
        factory.InitSpecialSpells();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceMounts))
        factory.InitMounts();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceGlyphs))
        ApplyGlyphStateForCap(bot, setupCap);

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceKeyring))
        factory.InitKeyring();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceGemsEnchants) && bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();

    bot->DurabilityRepairAll(false, 1.0f, false);
    bot->SendTalentsInfoData(false);
    ResetBotAIAndActions(botAI);

    if (bot->getClass() == CLASS_PALADIN)
    {
        ResolvedSpec resolved;
        if (ResolveCurrentSpec(bot, resolved))
            NormalizePaladinRighteousFury(bot, botAI, resolved.definition);
    }

    if (bot->getClass() == CLASS_HUNTER)
    {
        if (savedPetSpec && bot->GetLevel() >= 10)
        {
            if (!ConfigureHunterPetSpec(bot, savedPetSpec.value(), botAI->IsAlt(), errorMessage))
                return false;
        }
        else if (bot->GetPet())
            SetPetTankState(bot, false);
    }
    else if (bot->getClass() == CLASS_WARLOCK)
    {
        ResolvedSpec resolved;
        if (!ResolveCurrentSpec(bot, resolved) || !resolved.definition)
        {
            errorMessage = "could not determine current warlock spec.";
            return false;
        }

        Optional<PetSpecChoice> savedPetSpec = LoadSavedPetSpec(bot);
        PetSpecChoice const effectivePetSpec = savedPetSpec ? savedPetSpec.value() : PetSpecChoice::Dps;
        if (!ConfigureWarlockPetSpec(bot, botAI, resolved.definition, effectivePetSpec, errorMessage))
            return false;
    }

    return true;
}

bool ExecuteRestockCommand(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI)
        return false;

    PlayerbotFactory factory(bot, bot->GetLevel());
    bool const isAltBot = botAI->IsAlt();
    auto const shouldRun = [isAltBot](bool altGate) { return !isAltBot || altGate; };

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceAmmo))
        factory.InitAmmo();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceFood))
        factory.InitFood();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceReagents))
        factory.InitReagents();

    if (shouldRun(sPlayerbotAIConfig.altMaintenanceConsumables))
        factory.InitConsumables();

    if (shouldRun(sPlayerbotAIConfig.altMaintenancePotions))
        factory.InitPotions();

    bot->DurabilityRepairAll(false, 1.0f, false);
    return true;
}

bool ExecuteSpecCommand(Player* commandSender, Player* bot, PlayerbotAI* botAI, ModuleConfig const& config,
                        ParsedBotCommand const& command, std::string& errorMessage)
{
    if (!bot || !botAI)
    {
        errorMessage = "bot AI is not available.";
        return false;
    }

    ResolvedSpec resolved;
    if (!ResolveRequestedSpec(bot, command.specProfile, resolved, true) || !resolved.definition)
    {
        errorMessage = "invalid profile '" + command.specProfile + "' for " + bot->GetName() + ". " + BuildSpecListMessage(bot);
        return false;
    }

    int specNo = FindSpecNoForDefinition(bot->getClass(), *resolved.definition);
    if (specNo < 0)
    {
        errorMessage = "no matching premade template found for '" + FormatCanonicalName(resolved.definition->canonical) +
                       "' on " + bot->GetName() + '.';
        return false;
    }

    SyncAddclassBotLevel(bot, commandSender);

    ExpansionCap const cap = ResolveExpansionCap(bot, config);
    if (!ApplySpecTalents(bot, specNo, cap))
    {
        errorMessage = "failed to apply spec for " + bot->GetName() + '.';
        return false;
    }

    LearnBotSpellsForCurrentLevel(bot);
    if (config.autoGearRndBots)
        ApplyClassBotGearAgainstMaster(bot, commandSender, config);
    ApplyGlyphStateForCap(bot, cap);

    PlayerbotFactory factory(bot, bot->GetLevel());
    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();

    Optional<PetSpecChoice> savedPetSpec = LoadSavedPetSpec(bot);
    if (bot->getClass() == CLASS_HUNTER)
    {
        if (!savedPetSpec)
        {
            factory.InitPet();
            factory.InitPetTalents();
            SetPetTankState(bot, false);
        }
    }

    bot->SendTalentsInfoData(false);
    ResetBotAIAndActions(botAI);

    if (bot->getClass() == CLASS_PALADIN)
        NormalizePaladinRighteousFury(bot, botAI, resolved.definition);

    if (bot->getClass() == CLASS_HUNTER && savedPetSpec && bot->GetLevel() >= 10)
    {
        if (!ConfigureHunterPetSpec(bot, savedPetSpec.value(), botAI->IsAlt(), errorMessage))
            return false;
    }

    if (bot->getClass() == CLASS_WARLOCK)
    {
        PetSpecChoice const effectivePetSpec = savedPetSpec ? savedPetSpec.value() : PetSpecChoice::Dps;
        if (!ConfigureWarlockPetSpec(bot, botAI, resolved.definition, effectivePetSpec, errorMessage))
            return false;
    }

    return true;
}

bool ExecutePetSpecCommand(Player* bot, PlayerbotAI* botAI, ParsedBotCommand const& command, std::string& errorMessage)
{
    if (!bot || !botAI)
    {
        errorMessage = "bot AI is not available.";
        return false;
    }

    if (!SupportsPetSpecCommand(bot))
    {
        errorMessage = "petspec is only available for hunters and warlocks.";
        return false;
    }

    if (bot->getClass() == CLASS_HUNTER)
    {
        if (!ConfigureHunterPetSpec(bot, command.petSpecChoice, botAI->IsAlt(), errorMessage))
            return false;

        SavePetSpec(bot, command.petSpecChoice);
        return true;
    }

    ResolvedSpec resolved;
    if (!ResolveCurrentSpec(bot, resolved) || !resolved.definition)
    {
        errorMessage = "could not determine current warlock spec.";
        return false;
    }

    if (!ConfigureWarlockPetSpec(bot, botAI, resolved.definition, command.petSpecChoice, errorMessage))
        return false;

    SavePetSpec(bot, command.petSpecChoice);
    return true;
}

/* Reuse the same internals as chat commands "reset" and "reset botAI". */

void ResetBotAIAndActions(PlayerbotAI* botAI)
{
    if (!botAI)
        return;

    botAI->Reset(true);
    PlayerbotRepository::instance().Reset(botAI);
    botAI->ResetStrategies(false);
}

struct CommandResult
{
    uint32 matched = 0;
    uint32 updated = 0;
    uint32 failed = 0;
    bool handled = false;
};

char const* GetCommandLabel(BotCommandType type)
{
    switch (type)
    {
        case BotCommandType::Setup:
            return "setup";
        case BotCommandType::Spec:
            return "spec";
        case BotCommandType::Restock:
            return "restock";
        case BotCommandType::PetSpec:
            return "petspec";
        case BotCommandType::None:
        default:
            return "bettersetup";
    }
}

bool CheckMasterControl(Player* commandSender, Player* bot, ModuleConfig const& config)
{
    /* Config can disable ownership checks entirely for wide-open admin setups. */

    if (!config.requireMasterControl)
        return true;

    if (!commandSender || !commandSender->GetSession())
        return false;

    /* GM bypass exists for admin triage and operational emergencies. */

    if (commandSender->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        return true;

    /* Default path: only the owning master gets to command the bot. */

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI && botAI->GetMaster() == commandSender;
}

bool ProcessModuleCommandsForBot(Player* commandSender, uint32 chatType, std::string const& originalMessage, Player* bot,
                                 ModuleConfig const& config, CommandResult& result)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, chatType != CHAT_MSG_WHISPER, commandSender))
        return false;

    std::vector<std::string> commands = SplitCommands(originalMessage, sPlayerbotAIConfig.commandSeparator);
    bool processedAny = false;

    for (std::string command : commands)
    {
        command = TrimCopy(command);
        if (command.empty())
            continue;

        if (!sPlayerbotAIConfig.commandPrefix.empty())
        {
            if (!StartsWith(command, sPlayerbotAIConfig.commandPrefix))
                continue;

            command = TrimCopy(command.substr(sPlayerbotAIConfig.commandPrefix.size()));
            if (command.empty())
                continue;
        }

        std::string filtered = command;
        CompositeChatFilter selectorFilter(botAI);
        filtered = TrimCopy(selectorFilter.Filter(filtered));
        if (filtered.empty())
            continue;

        ParsedBotCommand parsed = ParseBotCommand(filtered);
        if (parsed.type == BotCommandType::None)
            continue;

        processedAny = true;
        result.handled = true;
        result.matched++;

        char const* label = GetCommandLabel(parsed.type);
        if (!CheckMasterControl(commandSender, bot, config))
        {
            result.failed++;
            botAI->TellMasterNoFacing(std::string(label) + ": command rejected for " + bot->GetName() + " (master control required).");
            continue;
        }

        if (parsed.listOnly)
        {
            if (parsed.type == BotCommandType::Spec && config.showSpecListOnEmpty)
                botAI->TellMasterNoFacing(BuildSpecListMessage(bot));
            else if (parsed.type == BotCommandType::PetSpec)
                botAI->TellMasterNoFacing(BuildPetSpecListMessage(bot));

            continue;
        }

        if (!parsed.errorMessage.empty())
        {
            result.failed++;
            botAI->TellMasterNoFacing(std::string(label) + ": " + parsed.errorMessage);
            continue;
        }

        std::string errorMessage;
        bool success = false;

        switch (parsed.type)
        {
            case BotCommandType::Setup:
                success = ExecuteSetupCommand(commandSender, bot, botAI, config, errorMessage);
                break;
            case BotCommandType::Spec:
                success = ExecuteSpecCommand(commandSender, bot, botAI, config, parsed, errorMessage);
                break;
            case BotCommandType::Restock:
                success = ExecuteRestockCommand(bot, botAI);
                break;
            case BotCommandType::PetSpec:
                success = ExecutePetSpecCommand(bot, botAI, parsed, errorMessage);
                break;
            case BotCommandType::None:
            default:
                break;
        }

        if (!success)
        {
            result.failed++;
            if (errorMessage.empty())
                errorMessage = "command failed for " + bot->GetName() + '.';
            botAI->TellMasterNoFacing(std::string(label) + ": " + errorMessage);
            continue;
        }

        result.updated++;
    }

    return processedAny;
}

/* Group targeting with de-duplication so one bot does not process the same order twice. */

std::vector<Player*> CollectGroupBots(Group* group)
{
    std::vector<Player*> bots;

    if (!group)
        return bots;

    std::set<ObjectGuid> seen;

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !GET_PLAYERBOT_AI(member))
            continue;

        if (!seen.insert(member->GetGUID()).second)
            continue;

        bots.push_back(member);
    }

    return bots;
}

/* Guild chat targeting for controlled bots owned by the command sender's manager. */

std::vector<Player*> CollectGuildBots(Player* commandSender)
{
    std::vector<Player*> bots;

    PlayerbotMgr* manager = GET_PLAYERBOT_MGR(commandSender);
    if (!manager)
        return bots;

    std::set<ObjectGuid> seen;

    for (auto itr = manager->GetPlayerBotsBegin(); itr != manager->GetPlayerBotsEnd(); ++itr)
    {
        Player* bot = itr->second;
        if (!bot || !GET_PLAYERBOT_AI(bot))
            continue;

        if (bot->GetGuildId() != commandSender->GetGuildId())
            continue;

        if (!seen.insert(bot->GetGUID()).second)
            continue;

        bots.push_back(bot);
    }

    return bots;
}

/* Channel targeting, including named channels and random bot population.
 * Membership is checked through ChannelMgr to avoid touching private Channel internals.
 * It is not glamorous work, but neither is mopping after a desynced channel list.
 */

std::vector<Player*> CollectChannelBots(Player* commandSender, Channel* channel)
{
    std::vector<Player*> bots;
    if (!channel)
        return bots;

    std::set<ObjectGuid> seen;
    std::string const channelName = channel->GetName();

    /* Pass one: managed playerbots from this master's manager context. */

    if (PlayerbotMgr* manager = GET_PLAYERBOT_MGR(commandSender))
    {
        if (channel->GetFlags() & 0x18)
        {
            for (auto itr = manager->GetPlayerBotsBegin(); itr != manager->GetPlayerBotsEnd(); ++itr)
            {
                Player* bot = itr->second;
                if (!bot || !GET_PLAYERBOT_AI(bot))
                    continue;

                if (ChannelMgr* channelMgr = ChannelMgr::forTeam(bot->GetTeamId()))
                {
                    if (!channelMgr->GetChannel(channelName, bot, false))
                        continue;
                }
                else
                {
                    continue;
                }

                if (!seen.insert(bot->GetGUID()).second)
                    continue;

                bots.push_back(bot);
            }
        }
    }

    /* Pass two: random bot pool currently present in the same channel. */

    for (auto itr = sRandomPlayerbotMgr.GetPlayerBotsBegin(); itr != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++itr)
    {
        Player* bot = itr->second;
        if (!bot || !GET_PLAYERBOT_AI(bot))
            continue;

        if (ChannelMgr* channelMgr = ChannelMgr::forTeam(bot->GetTeamId()))
        {
            if (!channelMgr->GetChannel(channelName, bot, false))
                continue;
        }
        else
            continue;

        if (!seen.insert(bot->GetGUID()).second)
            continue;

        bots.push_back(bot);
    }

    return bots;
}

void ReportSummary(Player* commandSender, CommandResult const& result)
{
    if (!result.handled)
        return;

    ChatHandler handler(commandSender->GetSession());

    std::ostringstream out;
    out << "bettersetup: matched " << result.matched << ", updated " << result.updated << ", failed " << result.failed << '.';
    handler.SendSysMessage(out.str());
}

/* Load config once per incoming chat event, fan out to chosen targets, then summarize. */

void ProcessTargets(Player* commandSender, uint32 chatType, std::string const& message, std::vector<Player*> const& targets)
{
    if (!commandSender || !commandSender->GetSession())
        return;

    /* One config snapshot per incoming message keeps behavior consistent per fan-out. */

    ModuleConfig const config = LoadModuleConfig();
    if (!config.enabled)
        return;

    CommandResult result;

    for (Player* bot : targets)
    {
        if (!bot)
            continue;

        ProcessModuleCommandsForBot(commandSender, chatType, message, bot, config, result);
    }

    ReportSummary(commandSender, result);
}

uint32 GetSpecPlayerTargetAverageIlvl(uint8 targetLevel, ModuleConfig const& config)
{
    switch (targetLevel)
    {
        case 60:
            return config.specPlayerTargetAverageIlvl60;
        case 70:
            return config.specPlayerTargetAverageIlvl70;
        default:
            return targetLevel;
    }
}

void ApplySpecPlayerGear(Player* player, uint8 targetLevel, ModuleConfig const& config)
{
    float const targetAverageIlvl = static_cast<float>(GetSpecPlayerTargetAverageIlvl(targetLevel, config));
    uint32 const targetIlvl = static_cast<uint32>(targetAverageIlvl);
    uint32 gearScoreLimit = ComputeGearScoreLimitFromAverageIlvl(targetAverageIlvl);

    for (uint8 attempt = 0; attempt < config.specPlayerGearRetryCount; ++attempt)
    {
        DestroyOldGear(player);

        /* Specplayer should stay in green/blue/purple bands and also correct
         * individual outlier slots toward the requested average ilvl.
         */
        RunGearPass(player, gearScoreLimit, config.specPlayerGearQualityCap, targetAverageIlvl, config, true,
                    config.specPlayerGearLevelSearchWindow);

        uint32 const currentIlvl = static_cast<uint32>(player->GetAverageItemLevelForDF());
        if (currentIlvl == 0 || IsSpecPlayerGearWithinTargetBand(player, targetAverageIlvl, config))
            break;

        float const scaled = static_cast<float>(gearScoreLimit) * static_cast<float>(targetIlvl) / static_cast<float>(currentIlvl);
        uint32 nextLimit = static_cast<uint32>(scaled);

        if (nextLimit == gearScoreLimit)
        {
            if (currentIlvl > targetIlvl && nextLimit > 1)
                --nextLimit;
            else if (currentIlvl < targetIlvl)
                ++nextLimit;
        }

        gearScoreLimit = std::max<uint32>(1, nextLimit);
    }
}


bool ParseOfflineSpecPlayerData(std::string const& data, std::string& canonicalSpec, uint8& level, ProfessionPair& professions)
{
    std::vector<std::string> tokens;
    std::stringstream stream(data);
    std::string token;

    while (std::getline(stream, token, '|'))
        tokens.push_back(token);

    if (tokens.size() != 2 && tokens.size() != 4)
        return false;

    canonicalSpec = tokens[0];
    if (canonicalSpec.empty())
        return false;

    std::stringstream levelStream(tokens[1]);
    uint32 parsedLevel = 0;
    if (!(levelStream >> parsedLevel))
        return false;

    if (parsedLevel < 1 || parsedLevel > 255)
        return false;

    level = static_cast<uint8>(parsedLevel);

    professions = { 0, 0 };
    if (tokens.size() == 4)
    {
        uint32 first = 0;
        uint32 second = 0;

        std::stringstream firstStream(tokens[2]);
        std::stringstream secondStream(tokens[3]);
        if (!(firstStream >> first) || !(secondStream >> second))
            return false;

        if (first > std::numeric_limits<uint16>::max() || second > std::numeric_limits<uint16>::max())
            return false;

        professions = { static_cast<uint16>(first), static_cast<uint16>(second) };
    }

    return true;
}

bool LoadOfflineSpecPlayerRequest(ObjectGuid::LowType guidLow, std::string& canonicalSpec, uint8& level, ProfessionPair& professions)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT data FROM character_settings WHERE guid = {} AND source = '{}' LIMIT 1",
        guidLow, OFFLINE_SPECPLAYER_SOURCE);

    if (!result)
        return false;

    std::string const data = (*result)[0].Get<std::string>();
    return ParseOfflineSpecPlayerData(data, canonicalSpec, level, professions);
}

void SaveOfflineSpecPlayerRequest(ObjectGuid::LowType guidLow, std::string const& canonicalSpec, uint8 level, ProfessionPair professions)
{
    std::string const data = canonicalSpec + "|" + std::to_string(uint32(level)) + "|" +
                             std::to_string(uint32(professions.first)) + "|" +
                             std::to_string(uint32(professions.second));
    CharacterDatabase.Execute(
        "REPLACE INTO character_settings (guid, source, data) VALUES ({}, '{}', '{}')",
        guidLow, OFFLINE_SPECPLAYER_SOURCE, data);
}

void ClearOfflineSpecPlayerRequest(ObjectGuid::LowType guidLow)
{
    CharacterDatabase.Execute(
        "DELETE FROM character_settings WHERE guid = {} AND source = '{}'",
        guidLow, OFFLINE_SPECPLAYER_SOURCE);
}

void ApplyRequestedPrimaryProfessions(Player* target, ProfessionPair professions)
{
    if (!target || (professions.first == 0 && professions.second == 0))
        return;

    for (uint16 skillId : GetPrimaryProfessionSkillIds())
    {
        if (target->HasSkill(skillId))
            target->SetSkill(skillId, 0, 0, 0);
    }

    uint16 const skillCap = ComputeLevelSkillCap(target);
    uint16 const step = GetSkillStepForValue(skillCap);

    target->SetSkill(professions.first, step, skillCap, skillCap);
    target->SetSkill(professions.second, step, skillCap, skillCap);
}

void NormalizeKnownSkillsToLevelCap(Player* target)
{
    if (!target)
        return;

    uint16 const skillCap = ComputeLevelSkillCap(target);

    for (uint32 skillId = 1; skillId < sSkillLineStore.GetNumRows(); ++skillId)
    {
        SkillLineEntry const* skillLine = sSkillLineStore.LookupEntry(skillId);
        if (!skillLine || !target->HasSkill(skillId))
            continue;

        if (skillLine->categoryId == SKILL_CATEGORY_LANGUAGES)
            continue;

        uint16 step = target->GetSkillStep(static_cast<uint16>(skillId));
        if (step == 0)
            step = 1;

        target->SetSkill(static_cast<uint16>(skillId), step, skillCap, skillCap);
    }
}

bool ApplySpecPlayerSetup(Player* target, std::string const& requestedProfile, uint8 targetLevel, ModuleConfig const& config,
                          ProfessionPair professions, std::string& appliedCanonical, std::string& errorMessage)
{
    if (!target)
    {
        errorMessage = "target player is not available.";
        return false;
    }

    ResolvedSpec resolved;
    if (!ResolveRequestedSpec(target, requestedProfile, resolved) || !resolved.definition)
    {
        errorMessage = "invalid profile '" + requestedProfile + "' for " + target->GetName() + ". " + BuildSpecListMessage(target);
        return false;
    }

    int specNo = FindSpecNoForDefinition(target->getClass(), *resolved.definition);
    if (specNo < 0)
    {
        errorMessage = "no matching premade template found for '" + FormatCanonicalName(resolved.definition->canonical) +
                       "' on " + target->GetName() + '.';
        return false;
    }

    target->CombatStop(true);
    target->GiveLevel(targetLevel);
    target->InitTalentForLevel();
    target->SetUInt32Value(PLAYER_XP, 0);

    ExpansionCap const cap = ResolveExpansionCap(target, config);
    if (!ApplySpecTalents(target, specNo, cap))
    {
        errorMessage = "failed to apply spec for " + target->GetName() + '.';
        return false;
    }

    RunSpecPlayerPostSpecMaintenance(target, cap, config);
    LearnSpellsForCurrentLevel(target);
    if (config.specPlayerApplyPrimaryProfessions)
        ApplyRequestedPrimaryProfessions(target, professions);
    if (config.specPlayerNormalizeKnownSkills)
        NormalizeKnownSkillsToLevelCap(target);
    if (config.specPlayerRemoveLevel60EpicClassMountSpells)
        RemoveLevel60EpicClassMountSpellsForSpecPlayer(target);
    NormalizeSpecPlayerRidingForLevel(target, config);
    ApplySpecPlayerGear(target, targetLevel, config);

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(target))
        ResetBotAIAndActions(botAI);

    appliedCanonical = resolved.definition->canonical;
    return true;
}

void ProcessOfflineSpecPlayerOnLogin(Player* player)
{
    if (!player || !player->GetSession())
        return;

    std::string canonicalSpec;
    uint8 targetLevel = 1;
    ProfessionPair professions = { 0, 0 };
    if (!LoadOfflineSpecPlayerRequest(player->GetGUID().GetCounter(), canonicalSpec, targetLevel, professions))
        return;

    ModuleConfig const config = LoadModuleConfig();
    if (!config.enabled)
        return;

    ChatHandler handler(player->GetSession());

    if (!config.specPlayerAllowOfflineQueue)
    {
        ClearOfflineSpecPlayerRequest(player->GetGUID().GetCounter());
        handler.SendSysMessage("specplayer: pending offline setup discarded because PlayerbotBetterSetup.SpecPlayer.AllowOfflineQueue = 0.");
        return;
    }

    std::string appliedCanonical;
    std::string errorMessage;
    bool const applied = ApplySpecPlayerSetup(player, canonicalSpec, targetLevel, config, professions, appliedCanonical, errorMessage);

    ClearOfflineSpecPlayerRequest(player->GetGUID().GetCounter());

    if (!applied)
    {
        handler.SendSysMessage("specplayer: pending offline setup failed and was discarded: " + errorMessage);
        return;
    }

    uint32 const currentAvgIlvl = static_cast<uint32>(player->GetAverageItemLevelForDF());
    if (professions.first != 0 && professions.second != 0)
    {
        handler.PSendSysMessage("specplayer: pending offline setup applied -> level {}, spec '{}', professions '{} + {}', current average ilvl {}.",
                                uint32(player->GetLevel()),
                                FormatCanonicalName(appliedCanonical),
                                ProfessionSkillToName(professions.first),
                                ProfessionSkillToName(professions.second),
                                currentAvgIlvl);
    }
    else
    {
        handler.PSendSysMessage("specplayer: pending offline setup applied -> level {}, spec '{}', current average ilvl {}.",
                                uint32(player->GetLevel()),
                                FormatCanonicalName(appliedCanonical),
                                currentAvgIlvl);
    }
}

class PlayerbotBetterSetupCommandScript final : public CommandScript
{
public:
    PlayerbotBetterSetupCommandScript()
        : CommandScript("PlayerbotBetterSetupCommandScript")
    {
    }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable commandTable =
        {
            { "specplayer", HandleSpecPlayerCommand, SEC_PLAYER, Acore::ChatCommands::Console::Yes }
        };

        return commandTable;
    }

    static bool HandleSpecPlayerCommand(ChatHandler* handler, Acore::ChatCommands::PlayerIdentifier targetIdentifier,
                                        std::string specProfile, uint32 requestedLevel,
                                        Optional<std::string> skill1Arg,
                                        Optional<std::string> skill2Arg)
    {
        if (!handler)
            return false;

        ModuleConfig const config = LoadModuleConfig();
        if (!config.enabled)
        {
            handler->SendSysMessage("specplayer: module is disabled (PlayerbotBetterSetup.Spec.Enable = 0).");
            return false;
        }

        uint8 const accountLevel = handler->IsConsole()
            ? uint8(SEC_CONSOLE)
            : static_cast<uint8>(handler->GetSession()->GetSecurity());
        if (!handler->IsConsole() && accountLevel < config.specPlayerMinSecurity)
        {
            handler->PSendSysMessage("specplayer: account level {} required (current {}).",
                                     uint32(config.specPlayerMinSecurity), uint32(accountLevel));
            return false;
        }

        ProfessionPair requestedProfessions = { 0, 0 };
        std::string professionError;
        if (!ParseRequestedProfessions(skill1Arg, skill2Arg, requestedProfessions, professionError))
        {
            handler->SendSysMessage("specplayer: " + professionError);
            return false;
        }

        Player* target = targetIdentifier.GetConnectedPlayer();
        ObjectGuid const targetGuid = targetIdentifier.GetGUID();
        if (target)
        {
            if (handler->HasLowerSecurity(target))
                return false;
        }
        else if (handler->HasLowerSecurity(nullptr, targetGuid))
            return false;

        if (requestedLevel < 1)
        {
            handler->SendSysMessage("specplayer: level must be >= 1.");
            return false;
        }

        uint32 const maxLevel = std::max<uint32>(1, sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        uint8 const targetLevel = static_cast<uint8>(std::clamp<uint32>(requestedLevel, 1, maxLevel));
        if (requestedLevel != targetLevel)
            handler->PSendSysMessage("specplayer: requested level {} clamped to {}.", requestedLevel, uint32(targetLevel));

        uint8 targetClass = 0;
        if (target)
        {
            targetClass = target->getClass();
        }
        else
        {
            CharacterCacheEntry const* cacheEntry = sCharacterCache->GetCharacterCacheByGuid(targetGuid);
            if (!cacheEntry)
            {
                handler->PSendSysMessage("specplayer: failed to read class data for {}.", targetIdentifier.GetName());
                return false;
            }

            targetClass = cacheEntry->Class;
        }

        ResolvedSpec resolved;
        if (!ResolveRequestedSpec(targetClass, specProfile, resolved) || !resolved.definition)
        {
            handler->PSendSysMessage("specplayer: invalid profile '{}' for {}. {}",
                                     specProfile, targetIdentifier.GetName(), BuildSpecListMessageForClass(targetClass));
            return false;
        }

        int specNo = FindSpecNoForDefinition(targetClass, *resolved.definition);
        if (specNo < 0)
        {
            handler->PSendSysMessage("specplayer: no matching premade template found for '{}' on {}.",
                                     FormatCanonicalName(resolved.definition->canonical), targetIdentifier.GetName());
            return false;
        }

        std::string const resolvedCanonical = resolved.definition->canonical;

        if (!target)
        {
            if (!config.specPlayerAllowOfflineQueue)
            {
                handler->SendSysMessage("specplayer: offline queue is disabled (PlayerbotBetterSetup.SpecPlayer.AllowOfflineQueue = 0).");
                return false;
            }

            SaveOfflineSpecPlayerRequest(targetGuid.GetCounter(), resolvedCanonical, targetLevel, requestedProfessions);
            if (requestedProfessions.first != 0 && requestedProfessions.second != 0)
            {
                handler->PSendSysMessage("specplayer: {} is offline. queued level {}, spec '{}', professions '{} + {}' for next login.",
                                         targetIdentifier.GetName(),
                                         uint32(targetLevel),
                                         FormatCanonicalName(resolvedCanonical),
                                         ProfessionSkillToName(requestedProfessions.first),
                                         ProfessionSkillToName(requestedProfessions.second));
            }
            else
            {
                handler->PSendSysMessage("specplayer: {} is offline. queued level {}, spec '{}' for next login.",
                                         targetIdentifier.GetName(),
                                         uint32(targetLevel),
                                         FormatCanonicalName(resolvedCanonical));
            }
            return true;
        }

        std::string appliedCanonical;
        std::string errorMessage;
        if (!ApplySpecPlayerSetup(target, resolvedCanonical, targetLevel, config, requestedProfessions, appliedCanonical, errorMessage))
        {
            handler->SendSysMessage("specplayer: " + errorMessage);
            return false;
        }

        uint32 const currentAvgIlvl = static_cast<uint32>(target->GetAverageItemLevelForDF());
        if (requestedProfessions.first != 0 && requestedProfessions.second != 0)
        {
            handler->PSendSysMessage("specplayer: {} -> level {}, spec '{}', professions '{} + {}', current average ilvl {}.",
                                     target->GetName(),
                                     uint32(target->GetLevel()),
                                     FormatCanonicalName(appliedCanonical),
                                     ProfessionSkillToName(requestedProfessions.first),
                                     ProfessionSkillToName(requestedProfessions.second),
                                     currentAvgIlvl);
        }
        else
        {
            handler->PSendSysMessage("specplayer: {} -> level {}, spec '{}', current average ilvl {}.",
                                     target->GetName(),
                                     uint32(target->GetLevel()),
                                     FormatCanonicalName(appliedCanonical),
                                     currentAvgIlvl);
        }
        return true;
    }
};

/* Shared login diagnostics block so one message format serves every login hook path. */

void SendLoginDiagnostics(Player* player)
{
    if (!player || !player->GetSession())
        return;

    ModuleConfig const config = LoadModuleConfig();
    if (!config.loginDiagnosticsEnable)
        return;

    bool const individualProgressionEnabled = sConfigMgr->GetOption<bool>("IndividualProgression.Enable", false, false);

    uint8 progressionTier = 0;
    bool const hasProgressionTier = TryGetProgressionTierFromSettings(player->GetGUID().GetCounter(), progressionTier);
    ExpansionCap const expansionCap = ResolveExpansionCap(player, config);

    std::ostringstream expansionOut;
    expansionOut << ExpansionCapToString(expansionCap);

    if (!sPlayerbotAIConfig.limitTalentsExpansion)
    {
        expansionOut << " (AiPlayerbot.LimitTalentsExpansion=0)";
    }
    else if (config.expansionSource == "progression")
    {
        if (hasProgressionTier)
            expansionOut << " (progression tier " << uint32(progressionTier) << ")";
        else
            expansionOut << " (progression tier missing, level fallback)";
    }
    else if (config.expansionSource == "auto")
    {
        if (hasProgressionTier)
            expansionOut << " (auto -> progression tier " << uint32(progressionTier) << ")";
        else
            expansionOut << " (auto -> level fallback)";
    }
    else
    {
        expansionOut << " (level source)";
    }

    uint32 const masterIlvl = static_cast<uint32>(player->GetAverageItemLevelForDF());
    std::string const rndTarget = BuildTargetIlvlLabel(player, config.gearModeRndBots, config.gearRatioRndBots);
    std::string const altTarget = BuildTargetIlvlLabel(player, config.gearModeAltBots, config.gearRatioAltBots);

    ChatHandler handler(player->GetSession());
    handler.SendSysMessage("|cff00ff00mod-playerbot-bettersetup:|r loaded");
    handler.SendSysMessage(std::string("|cff00ff00Individual Progression:|r ") + (individualProgressionEnabled ? "loaded" : "not loaded/disabled"));
    handler.SendSysMessage("|cff00ff00Expansion used to determine gear:|r " + expansionOut.str());
    handler.PSendSysMessage("|cff00ff00Master average ilvl:|r {}", masterIlvl);
    handler.SendSysMessage("|cff00ff00Bot target ilvl (rnd/alt):|r " + rndTarget + " / " + altTarget);
}

class PlayerbotBetterSetupLoginScript final : public PlayerScript
{
public:
    PlayerbotBetterSetupLoginScript()
        : PlayerScript("PlayerbotBetterSetupLoginScript")
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        ProcessOfflineSpecPlayerOnLogin(player);
        SendLoginDiagnostics(player);
    }
};

class PlayerbotBetterSetupPlayerScript final : public PlayerScript
{
public:
    PlayerbotBetterSetupPlayerScript()
        : PlayerScript("PlayerbotBetterSetupPlayerScript",
                       { PLAYERHOOK_CAN_PLAYER_USE_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT })
    {
    }

    /* Generic chat path (say/yell/emote-like). */

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& msg) override
    {
        if (!player)
            return true;

        return true;
    }

    /* Whisper path: direct one-bot control. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Player* receiver) override
    {
        if (!player || !receiver)
            return true;

        if (!GET_PLAYERBOT_AI(receiver))
            return true;

        ProcessTargets(player, type, msg, { receiver });
        return true;
    }

    /* Group/raid path: selectors such as @group2 @warrior are evaluated per bot downstream. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Group* group) override
    {
        if (!player || !group)
            return true;

        ProcessTargets(player, type, msg, CollectGroupBots(group));
        return true;
    }

    /* Guild path: command fans out to guild bots available to this master context. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Guild* guild) override
    {
        if (!player || !guild || type != CHAT_MSG_GUILD)
            return true;

        ProcessTargets(player, type, msg, CollectGuildBots(player));
        return true;
    }

    /* Channel path: useful for mass commands in shared channels. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Channel* channel) override
    {
        if (!player || !channel)
            return true;

        ProcessTargets(player, type, msg, CollectChannelBots(player, channel));
        return true;
    }
};

} /* namespace */

void AddPlayerbotBetterSetupScripts()
{
    new PlayerbotBetterSetupCommandScript();
    new PlayerbotBetterSetupLoginScript();
    new PlayerbotBetterSetupPlayerScript();
}
