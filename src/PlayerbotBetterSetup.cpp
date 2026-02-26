/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
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
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
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
constexpr char const* CONF_LOGIN_DIAGNOSTICS_ENABLE = "PlayerbotBetterSetup.LoginDiagnostics.Enable";

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

std::string BuildSpecListMessage(Player* bot)
{
    auto const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(bot->getClass());
    if (profileIt == profiles.end())
        return "No spec profile is defined for this class.";

    ClassSpecProfile const& profile = profileIt->second;

    std::vector<std::string> roleParts;
    std::vector<std::string> roleOrder = { "tank", "heal", "melee", "ranged", "dps" };

    /* Build role summaries in a fixed order so output stays predictable for humans. */

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

    /* Build the exact list for players who prefer precision over destiny. */

    std::ostringstream exact;
    for (size_t i = 0; i < profile.specs.size(); ++i)
    {
        if (i != 0)
            exact << ", ";

        exact << FormatCanonicalName(profile.specs[i].canonical);
    }

    /* Stitch the final response sentence for whisper output. */

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

/* Parse "spec", "spec <profile>", and "spec <profile> gear".
 * Anything else is politely ignored so raid chat can continue discussing fish feasts.
 */

ParsedSpecCommand ParseSpecCommand(std::string const& command)
{
    ParsedSpecCommand parsed;

    std::vector<std::string> words = SplitWords(command);
    if (words.empty())
        return parsed;

    /* If it does not start with 'spec', we leave it alone and let chat be chat. */

    if (NormalizeToken(words.front()) != "spec")
        return parsed;

    parsed.isSpecCommand = true;

    if (words.size() == 1)
    {
        parsed.listOnly = true;
        return parsed;
    }

    /* Optional trailing gear flag is consumed last so profile parsing stays simple. */

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

    /* Whatever survives after the command verb is the requested profile token. */

    parsed.profile = JoinWords(words, 1);
    return parsed;
}

/* Parse a plain "gearself" command token. Extra words are ignored on purpose
 * so people can append notes without derailing the operation.
 */

bool IsGearSelfCommand(std::string const& command)
{
    std::vector<std::string> words = SplitWords(command);
    if (words.empty())
        return false;

    return NormalizeToken(words.front()) == "gearself";
}

struct ResolvedSpec
{
    SpecDefinition const* definition = nullptr;
    bool fromRole = false;
};

/* Resolve what the user asked into an exact spec definition.
 * - Exact aliases: deterministic.
 * - Role umbrella (tank/heal/melee/ranged/dps): uniform random among mapped options.
 * The random branch is intentionally fair; fate should at least be evenly distributed.
 */

bool ResolveRequestedSpec(Player* bot, std::string const& requestedProfile, ResolvedSpec& resolved)
{
    auto const& profiles = GetClassSpecProfiles();
    auto const profileIt = profiles.find(bot->getClass());
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
            resolved.fromRole = false;
            return true;
        }
    }

    /* No exact hit: treat input as a role umbrella and roll uniformly inside it. */

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

/* Decide which expansion cap to use for talent filtering.
 * If limitTalentsExpansion is disabled upstream, this always resolves to Wrath.
 * "auto" means progression tier first, then level if that data is missing.
 */

ExpansionCap ResolveExpansionCap(Player* bot, Player* commandSender, ModuleConfig const& config)
{
    /* If upstream expansion limiting is disabled, we keep our hands off the dial. */

    if (!sPlayerbotAIConfig.limitTalentsExpansion)
        return ExpansionCap::Wrath;

    std::string const mode = config.expansionSource;

    if (mode == "level")
        return GetLevelBasedCap(bot);

    /* Progression/auto tries character_settings first, then falls back to level bands. */

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

    /* Unknown mode values get the boring fallback so nobody gets paged at 3am. */

    return GetLevelBasedCap(bot);
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

/* Some caps do not support glyphs; when in doubt, wipe to a clean state. */

void ClearGlyphs(Player* bot)
{
    for (uint32 slotIndex = 0; slotIndex < MAX_GLYPH_SLOT_INDEX; ++slotIndex)
        bot->SetGlyph(slotIndex, 0, true);

    bot->SendTalentsInfoData(false);
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
    return true;
}

/* Rndbots and addclass bots share the same policy bucket. */

bool IsRndOrAddclassBot(Player* bot)
{
    return sRandomPlayerbotMgr.IsRandomBot(bot) || sRandomPlayerbotMgr.IsAddclassBot(bot);
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

/* One gearing pass (equipment/ammo/enchants/repair) with a chosen cap.
 * cap == 0 means top-for-level mode.
 */

void RunGearPass(Player* bot, uint32 gearScoreLimit, uint32 qualityLimit)
{
    PlayerbotFactory factory(bot, bot->GetLevel(), qualityLimit, gearScoreLimit);
    factory.InitEquipment(false, true);
    factory.InitAmmo();

    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();

    bot->DurabilityRepairAll(false, 1.0f, false);
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
                RunGearPass(bot, gearScoreLimit, config.gearQualityCapRatioMode);

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
 * glyphs, consumables, pet init/talents, and spell book refresh.
 */

void RunPostSpecRefresh(Player* bot, ExpansionCap cap)
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

    /* Final spell sweeps repopulate class, available, and special spell lists. */

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
    /* Config can disable ownership checks entirely for wide-open admin setups. */

    if (!config.requireMasterControl)
        return true;

    if (!commandSender || !commandSender->GetSession())
        return false;

    /* GM bypass exists for admin triage and operational emergencies. */

    if (commandSender->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        return true;

    /* Default path: only the owning master gets to rewire this bot's profession in life. */

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI && botAI->GetMaster() == commandSender;
}

/* Main per-bot pipeline:
 * 1) split/normalize chat command,
 * 2) apply selector filtering,
 * 3) resolve spec intent,
 * 4) apply talents + refresh + optional gear,
 * 5) report failures to master with context.
 * It reads long because it is a control tower, not because it forgot to stop.
 */

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
        /* Stage 1: trim and apply command prefix gate. */

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

        /* Stage 2: run selector filters (@group2, @warrior, etc.) and parse actual command text. */

        std::string filtered = command;
        CompositeChatFilter selectorFilter(botAI);
        filtered = selectorFilter.Filter(filtered);
        filtered = TrimCopy(filtered);

        if (filtered.empty())
            continue;

        ParsedSpecCommand spec = ParseSpecCommand(filtered);
        if (!spec.isSpecCommand)
            continue;

        /* Stage 3: mark command as handled/matched before policy checks. */

        processedAny = true;
        result.handled = true;
        result.matched++;

        /* Stage 4: enforce master control rules and handle list-only requests. */

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

        /* Stage 5: resolve requested profile and map it to a premade spec template index. */

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

        /* Stage 6: apply talents, run maintenance, and optionally auto-gear. */

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

    if (result.matched == 0)
    {
        handler.SendSysMessage("spec: no bots matched the selectors.");
        return;
    }

    std::ostringstream out;
    out << "spec: matched " << result.matched << ", updated " << result.updated << ", failed " << result.failed << '.';
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

        ProcessSpecForBot(commandSender, chatType, message, bot, config, result);
    }

    ReportSummary(commandSender, result);
}

/* Self-service test helper:
 * gears the player toward target average ilvl = character level.
 * We steer the factory cap using small feedback iterations.
 */

void ApplyGearSelf(Player* player)
{
    uint32 const targetIlvl = player->GetLevel();
    uint32 gearScoreLimit = PlayerbotFactory::CalcMixedGearScore(targetIlvl, ITEM_QUALITY_NORMAL);

    for (uint8 attempt = 0; attempt < 6; ++attempt)
    {
        PlayerbotFactory factory(player, player->GetLevel(), ITEM_QUALITY_LEGENDARY, gearScoreLimit);
        factory.InitEquipment(false);
        factory.InitAmmo();

        if (player->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
            factory.ApplyEnchantAndGemsNew();

        player->DurabilityRepairAll(false, 1.0f, false);

        uint32 const currentIlvl = static_cast<uint32>(player->GetAverageItemLevelForDF());
        if (currentIlvl == targetIlvl || currentIlvl == 0)
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

/* Parse incoming chat, honor command prefix/separator, and execute gearself once
 * for each explicit appearance in the message.
 */

void ProcessSelfCommands(Player* commandSender, std::string const& originalMessage)
{
    if (!commandSender || !commandSender->GetSession())
        return;

    ModuleConfig const config = LoadModuleConfig();
    if (!config.enabled)
        return;

    std::vector<std::string> commands = SplitCommands(originalMessage, sPlayerbotAIConfig.commandSeparator);
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

        if (!IsGearSelfCommand(command))
            continue;

        ChatHandler handler(commandSender->GetSession());

        if (commandSender->GetSession()->GetSecurity() < SEC_GAMEMASTER)
        {
            handler.SendSysMessage("gearself: GM permission required.");
            continue;
        }

        ApplyGearSelf(commandSender);
        uint32 const currentAvgIlvl = static_cast<uint32>(commandSender->GetAverageItemLevelForDF());
        handler.PSendSysMessage("gearself: target average ilvl {} (from level), current average ilvl {}.",
                                commandSender->GetLevel(), currentAvgIlvl);
    }
}

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
    ExpansionCap const expansionCap = ResolveExpansionCap(player, player, config);

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

        ProcessSelfCommands(player, msg);
        return true;
    }

    /* Whisper path: direct one-bot control. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Player* receiver) override
    {
        if (!player || !receiver)
            return true;

        ProcessSelfCommands(player, msg);

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

        ProcessSelfCommands(player, msg);

        ProcessTargets(player, type, msg, CollectGroupBots(group));
        return true;
    }

    /* Guild path: command fans out to guild bots available to this master context. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Guild* guild) override
    {
        if (!player || !guild || type != CHAT_MSG_GUILD)
            return true;

        ProcessSelfCommands(player, msg);

        ProcessTargets(player, type, msg, CollectGuildBots(player));
        return true;
    }

    /* Channel path: useful for mass commands in shared channels. */

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*language*/, std::string& msg, Channel* channel) override
    {
        if (!player || !channel)
            return true;

        ProcessSelfCommands(player, msg);

        ProcessTargets(player, type, msg, CollectChannelBots(player, channel));
        return true;
    }
};

} /* namespace */

void AddPlayerbotBetterSetupScripts()
{
    new PlayerbotBetterSetupLoginScript();
    new PlayerbotBetterSetupPlayerScript();
}
