/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"

#include "ChatFilter.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
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

struct ModuleConfig
{
    bool enabled = true;
    bool requireMasterControl = true;
    bool showSpecListOnEmpty = true;

    bool autoGearRndBots = true;
    bool autoGearAltBots = false;

    std::string gearModeRndBots = "master_ilvl_ratio";
    std::string gearModeAltBots = "master_ilvl_ratio";
    float gearRatioRndBots = 1.0f;
    float gearRatioAltBots = 1.0f;

    std::string expansionSource = "auto";
};

ModuleConfig LoadModuleConfig()
{
    ModuleConfig config;

    config.enabled = sConfigMgr->GetOption<bool>(CONF_SPEC_ENABLE, true);
    config.requireMasterControl = sConfigMgr->GetOption<bool>(CONF_REQUIRE_MASTER_CONTROL, true);
    config.showSpecListOnEmpty = sConfigMgr->GetOption<bool>(CONF_SHOW_SPEC_LIST_ON_EMPTY, true);

    config.autoGearRndBots = sConfigMgr->GetOption<bool>(CONF_AUTO_GEAR_RNDBOTS, true);
    config.autoGearAltBots = sConfigMgr->GetOption<bool>(CONF_AUTO_GEAR_ALTBOTS, false);

    config.gearModeRndBots = NormalizeToken(sConfigMgr->GetOption<std::string>(CONF_GEAR_MODE_RNDBOTS, "master_ilvl_ratio"));
    config.gearModeAltBots = NormalizeToken(sConfigMgr->GetOption<std::string>(CONF_GEAR_MODE_ALTBOTS, "master_ilvl_ratio"));
    config.gearRatioRndBots = sConfigMgr->GetOption<float>(CONF_GEAR_RATIO_RNDBOTS, 1.0f);
    config.gearRatioAltBots = sConfigMgr->GetOption<float>(CONF_GEAR_RATIO_ALTBOTS, 1.0f);

    config.expansionSource = NormalizeToken(sConfigMgr->GetOption<std::string>(CONF_EXPANSION_SOURCE, "auto"));

    if (config.gearRatioRndBots < 0.0f)
        config.gearRatioRndBots = 0.0f;

    if (config.gearRatioAltBots < 0.0f)
        config.gearRatioAltBots = 0.0f;

    return config;
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
                    { "beastmaster", { "beastmaster", "bm" }, { "bm", "beast" }, { 0 } },
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
                    { "blood_tank", { "blood_tank", "bloodtank", "bdkt" }, { "blood" }, { 0 } },
                    { "blood_dps", { "blood_dps", "blooddps", "bdkd" }, { "double aura blood", "blood dps", "blood" }, { 3, 0 } },
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
                    { "feral_tank", { "feral_tank", "feraltank", "bear" }, { "bear" }, { 1 } },
                    { "feral_dps", { "feral_dps", "feraldps", "cat" }, { "cat" }, { 3 } },
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

bool MatchPremadeNameByToken(std::string const& premadeNameLower, std::vector<std::string> const& tokens)
{
    if (tokens.empty())
        return false;

    std::vector<std::string> words = SplitWords(premadeNameLower);
    std::vector<std::string> normalizedWords;
    normalizedWords.reserve(words.size());
    for (std::string const& word : words)
        normalizedWords.push_back(NormalizeToken(word));

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

int FindSpecNoForDefinition(uint8 classId, SpecDefinition const& spec)
{
    auto const hasPremade = [&](uint8 specNo)
    {
        if (specNo >= MAX_SPECNO)
            return false;

        return !sPlayerbotAIConfig.premadeSpecName[classId][specNo].empty();
    };

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

std::string BuildSpecListMessage(Player* bot)
{
    auto const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(bot->getClass());
    if (profileIt == profiles.end())
        return "No spec profile is defined for this class.";

    ClassSpecProfile const& profile = profileIt->second;

    std::vector<std::string> roleParts;
    std::vector<std::string> roleOrder = { "tank", "heal", "melee", "ranged", "dps" };

    for (std::string const& role : roleOrder)
    {
        auto const roleIt = profile.roles.find(role);
        if (roleIt == profile.roles.end() || roleIt->second.empty())
            continue;

        if (roleIt->second.size() == 1)
        {
            roleParts.push_back(role + " (" + FormatCanonicalName(roleIt->second.front()) + ")");
            continue;
        }

        std::ostringstream options;
        for (size_t i = 0; i < roleIt->second.size(); ++i)
        {
            if (i != 0)
                options << '/';

            options << FormatCanonicalName(roleIt->second[i]);
        }

        roleParts.push_back(role + " (random " + options.str() + ")");
    }

    std::ostringstream exact;
    for (size_t i = 0; i < profile.specs.size(); ++i)
    {
        if (i != 0)
            exact << ", ";

        exact << FormatCanonicalName(profile.specs[i].canonical);
    }

    std::ostringstream message;
    message << "Valid specs: ";

    for (size_t i = 0; i < roleParts.size(); ++i)
    {
        if (i != 0)
            message << ", ";

        message << roleParts[i];
    }

    if (!roleParts.empty())
        message << ". ";

    message << "Exact: " << exact.str() << '.';
    return message.str();
}

struct ParsedSpecCommand
{
    bool isSpecCommand = false;
    bool listOnly = false;
    bool gearRequested = false;
    std::string profile;
};

ParsedSpecCommand ParseSpecCommand(std::string const& command)
{
    ParsedSpecCommand parsed;

    std::vector<std::string> words = SplitWords(command);
    if (words.empty())
        return parsed;

    if (NormalizeToken(words.front()) != "spec")
        return parsed;

    parsed.isSpecCommand = true;

    if (words.size() == 1)
    {
        parsed.listOnly = true;
        return parsed;
    }

    if (NormalizeToken(words.back()) == "gear")
    {
        parsed.gearRequested = true;
        words.pop_back();
    }

    if (words.size() == 1)
    {
        parsed.listOnly = true;
        return parsed;
    }

    parsed.profile = JoinWords(words, 1);
    return parsed;
}

struct ResolvedSpec
{
    SpecDefinition const* definition = nullptr;
    bool fromRole = false;
};

bool ResolveRequestedSpec(Player* bot, std::string const& requestedProfile, ResolvedSpec& resolved)
{
    auto const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(bot->getClass());
    if (profileIt == profiles.end())
        return false;

    ClassSpecProfile const& profile = profileIt->second;
    std::string const requestedNorm = NormalizeToken(requestedProfile);

    for (SpecDefinition const& exact : profile.specs)
    {
        for (std::string const& alias : exact.aliases)
        {
            if (requestedNorm != NormalizeToken(alias))
                continue;

            resolved.definition = &exact;
            resolved.fromRole = false;
            return true;
        }
    }

    auto const roleIt = profile.roles.find(requestedNorm);
    if (roleIt == profile.roles.end() || roleIt->second.empty())
        return false;

    std::vector<std::string> const& options = roleIt->second;
    uint32 selectedIndex = options.size() == 1 ? 0 : urand(0, options.size() - 1);
    std::string const& selectedCanonical = options[selectedIndex];

    resolved.definition = FindSpecDefinition(profile, selectedCanonical);
    resolved.fromRole = true;
    return resolved.definition != nullptr;
}

enum class ExpansionCap
{
    Wrath,
    TBC,
    Vanilla,
};

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

ExpansionCap GetProgressionBasedCap(uint8 progressionTier)
{
    if (progressionTier <= 7)
        return ExpansionCap::Vanilla;

    if (progressionTier <= 12)
        return ExpansionCap::TBC;

    return ExpansionCap::Wrath;
}

ExpansionCap ResolveExpansionCap(Player* bot, Player* commandSender, ModuleConfig const& config)
{
    if (!sPlayerbotAIConfig.limitTalentsExpansion)
        return ExpansionCap::Wrath;

    std::string const mode = config.expansionSource;

    if (mode == "level")
        return GetLevelBasedCap(bot);

    if (mode == "progression" || mode == "auto")
    {
        if (commandSender)
        {
            uint8 progressionTier = 0;
            if (TryGetProgressionTierFromSettings(commandSender->GetGUID().GetCounter(), progressionTier))
                return GetProgressionBasedCap(progressionTier);
        }

        return GetLevelBasedCap(bot);
    }

    return GetLevelBasedCap(bot);
}

bool IsAllowedTalentNode(ExpansionCap cap, uint32 row, uint32 col)
{
    if (cap == ExpansionCap::Vanilla)
        return !(row > 6 || (row == 6 && col != 1));

    if (cap == ExpansionCap::TBC)
        return !(row > 8 || (row == 8 && col != 1));

    return true;
}

std::vector<std::vector<uint32>> BuildTemplatePath(Player* bot, uint8 classId, int specNo)
{
    int startLevel = static_cast<int>(bot->GetLevel());

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

void ClearGlyphs(Player* bot)
{
    for (uint32 slotIndex = 0; slotIndex < MAX_GLYPH_SLOT_INDEX; ++slotIndex)
        bot->SetGlyph(slotIndex, 0, true);

    bot->SendTalentsInfoData(false);
}

bool ApplySpecTalents(Player* bot, int specNo, ExpansionCap cap)
{
    std::vector<std::vector<uint32>> parsedPath = BuildTemplatePath(bot, bot->getClass(), specNo);

    if (parsedPath.empty())
    {
        PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true);
        return true;
    }

    std::vector<std::vector<uint32>> filtered;
    filtered.reserve(parsedPath.size());

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

    if (filtered.empty())
    {
        PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true);
        return true;
    }

    PlayerbotFactory::InitTalentsByParsedSpecLink(bot, filtered, true);
    return true;
}

bool IsRndOrAddclassBot(Player* bot)
{
    return sRandomPlayerbotMgr.IsRandomBot(bot) || sRandomPlayerbotMgr.IsAddclassBot(bot);
}

bool ShouldAutoGear(Player* bot, bool gearRequested, ModuleConfig const& config)
{
    if (IsRndOrAddclassBot(bot))
        return config.autoGearRndBots;

    return config.autoGearAltBots && gearRequested;
}

uint32 ComputeGearScoreLimit(Player* commandSender, float ratio)
{
    if (!commandSender)
        return 0;

    uint32 const mixed = PlayerbotAI::GetMixedGearScore(commandSender, true, false, 12);
    if (mixed == 0)
        return 0;

    float const scaled = static_cast<float>(mixed) * ratio;
    if (scaled <= 0.0f)
        return 0;

    return static_cast<uint32>(scaled);
}

void ApplyAutoGear(Player* bot, Player* commandSender, ModuleConfig const& config)
{
    bool const rndbot = IsRndOrAddclassBot(bot);
    std::string mode = rndbot ? config.gearModeRndBots : config.gearModeAltBots;
    float const ratio = rndbot ? config.gearRatioRndBots : config.gearRatioAltBots;

    bool useMasterRatio = (mode == "masterilvlratio" || mode == "master_ilvl_ratio");
    uint32 gearScoreLimit = 0;

    if (useMasterRatio)
    {
        gearScoreLimit = ComputeGearScoreLimit(commandSender, ratio);
        if (gearScoreLimit == 0)
            useMasterRatio = false;
    }

    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY, useMasterRatio ? gearScoreLimit : 0);
    factory.InitEquipment(false);
    factory.InitAmmo();

    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();

    bot->DurabilityRepairAll(false, 1.0f, false);
}

void RunPostSpecRefresh(Player* bot, ExpansionCap cap)
{
    PlayerbotFactory factory(bot, bot->GetLevel());

    if (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion)
        factory.InitGlyphs(false);
    else
        ClearGlyphs(bot);

    factory.InitConsumables();
    factory.InitPet();

    if (cap == ExpansionCap::Wrath || !sPlayerbotAIConfig.limitTalentsExpansion)
        factory.InitPetTalents();

    factory.InitClassSpells();
    factory.InitAvailableSpells();
    factory.InitSpecialSpells();
}

struct CommandResult
{
    uint32 matched = 0;
    uint32 updated = 0;
    uint32 failed = 0;
    bool handled = false;
};

bool CheckMasterControl(Player* commandSender, Player* bot, ModuleConfig const& config)
{
    if (!config.requireMasterControl)
        return true;

    if (!commandSender || !commandSender->GetSession())
        return false;

    if (commandSender->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        return true;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI && botAI->GetMaster() == commandSender;
}

bool ProcessSpecForBot(Player* commandSender, uint32 chatType, std::string const& originalMessage, Player* bot,
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

            command = command.substr(sPlayerbotAIConfig.commandPrefix.size());
            command = TrimCopy(command);

            if (command.empty())
                continue;
        }

        std::string filtered = command;
        CompositeChatFilter selectorFilter(botAI);
        filtered = selectorFilter.Filter(filtered);
        filtered = TrimCopy(filtered);

        if (filtered.empty())
            continue;

        ParsedSpecCommand spec = ParseSpecCommand(filtered);
        if (!spec.isSpecCommand)
            continue;

        processedAny = true;
        result.handled = true;
        result.matched++;

        if (!CheckMasterControl(commandSender, bot, config))
        {
            result.failed++;
            botAI->TellMasterNoFacing("spec: command rejected for " + bot->GetName() + " (master control required).");
            continue;
        }

        if (spec.listOnly)
        {
            if (config.showSpecListOnEmpty)
                botAI->TellMasterNoFacing(BuildSpecListMessage(bot));

            continue;
        }

        ResolvedSpec resolved;
        if (!ResolveRequestedSpec(bot, spec.profile, resolved) || !resolved.definition)
        {
            result.failed++;
            botAI->TellMasterNoFacing("spec: invalid profile '" + spec.profile + "' for " + bot->GetName() + ". " + BuildSpecListMessage(bot));
            continue;
        }

        int specNo = FindSpecNoForDefinition(bot->getClass(), *resolved.definition);
        if (specNo < 0)
        {
            result.failed++;
            botAI->TellMasterNoFacing("spec: no matching premade template found for '" + FormatCanonicalName(resolved.definition->canonical) +
                                      "' on " + bot->GetName() + '.');
            continue;
        }

        ExpansionCap const cap = ResolveExpansionCap(bot, commandSender, config);

        if (!ApplySpecTalents(bot, specNo, cap))
        {
            result.failed++;
            botAI->TellMasterNoFacing("spec: failed to apply spec for " + bot->GetName() + '.');
            continue;
        }

        botAI->ResetStrategies();
        RunPostSpecRefresh(bot, cap);

        if (ShouldAutoGear(bot, spec.gearRequested, config))
            ApplyAutoGear(bot, commandSender, config);

        result.updated++;
    }

    return processedAny;
}

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

std::vector<Player*> CollectChannelBots(Player* commandSender, Channel* channel)
{
    std::vector<Player*> bots;
    if (!channel)
        return bots;

    std::set<ObjectGuid> seen;
    std::string const channelName = channel->GetName();

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

    if (result.matched == 0)
    {
        handler.SendSysMessage("spec: no bots matched the selectors.");
        return;
    }

    std::ostringstream out;
    out << "spec: matched " << result.matched << ", updated " << result.updated << ", failed " << result.failed << '.';
    handler.SendSysMessage(out.str());
}

void ProcessTargets(Player* commandSender, uint32 chatType, std::string const& message, std::vector<Player*> const& targets)
{
    if (!commandSender || !commandSender->GetSession())
        return;

    ModuleConfig const config = LoadModuleConfig();
    if (!config.enabled)
        return;

    CommandResult result;

    for (Player* bot : targets)
    {
        if (!bot)
            continue;

        ProcessSpecForBot(commandSender, chatType, message, bot, config, result);
    }

    ReportSummary(commandSender, result);
}

class PlayerbotBetterSetupPlayerScript final : public PlayerScript
{
public:
    PlayerbotBetterSetupPlayerScript()
        : PlayerScript("PlayerbotBetterSetupPlayerScript",
                       { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT })
    {
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Player* receiver) override
    {
        if (!player || !receiver)
            return true;

        if (!GET_PLAYERBOT_AI(receiver))
            return true;

        ProcessTargets(player, type, msg, { receiver });
        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Group* group) override
    {
        if (!player || !group)
            return true;

        ProcessTargets(player, type, msg, CollectGroupBots(group));
        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Guild* guild) override
    {
        if (!player || !guild || type != CHAT_MSG_GUILD)
            return true;

        ProcessTargets(player, type, msg, CollectGuildBots(player));
        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Channel* channel) override
    {
        if (!player || !channel)
            return true;

        ProcessTargets(player, type, msg, CollectChannelBots(player, channel));
        return true;
    }
};

} // namespace

void AddPlayerbotBetterSetupScripts()
{
    new PlayerbotBetterSetupPlayerScript();
}
