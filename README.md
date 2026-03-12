# mod-playerbot-bettersetup

`mod-playerbot-bettersetup` extends `mod-playerbots` with a unified bot spec workflow.

It adds one command family (`spec`) that works for both rnd/addclass bots and altbots, supports selectors (`@group2`, `@warrior`, etc.), applies expansion-aware talents, performs post-spec maintenance, and supports configurable autogear rules.

## Features

- Unified `spec` command for playerbots in whisper/group/raid/guild/channel contexts.
- Role and exact-spec targeting with aliases (for example `tank`, `holy`, `prot`, `mm`).
- Selector compatibility through existing playerbot chat filters (`@groupX`, `@class`, etc.).
- Expansion-aware talent application honoring `AiPlayerbot.LimitTalentsExpansion`.
- Optional progression-tier expansion source via `mod-individual-progression` (`character_settings`).
- Configurable autogear policy split between rnd/addclass bots and altbots.
- Gear target based on commanding player's average ilvl when using `master_ilvl_ratio` mode.
- Optional profession reassignment and known-skill normalization in `spec`.
- Post-spec maintenance for glyphs, consumables, pets, pet talents, and spell refresh.
- Optional login diagnostics in chat.
- Configurable-security `gearself` helper command for test gearing.
- Configurable `.specplayer` access, offline queueing, riding, skill normalization,
  and target-ilvl gearing.

## Commands

Use normal playerbot chat channels and prefixes.

- `spec`
- `spec <profile>`
- `spec <profile> <skill1> <skill2>`
- `spec <profile> gear`
- `spec <profile> <skill1> <skill2> gear`
- `gearself` (minimum account level is configurable)
- `.specplayer <name> <spec> <level> [skill1] [skill2]` (minimum security and offline queue behavior are configurable)

Examples:

- `spec`
- `spec tank`
- `spec holy`
- `spec prot`
- `spec protection engineering blacksmithing`
- `spec dps gear`
- `spec dps engineering mining gear`
- `@group2 @warrior spec tank`
- `@group2 @warrior spec dps gear`
- `.specplayer Farsong marksman 60`
- `.specplayer Farsong marksman 60 skinning leatherworking`

### `spec` behavior

- `spec` with no argument returns valid role/spec options for the target bot class.
- `<profile>` can be a role or an exact spec alias.
- Optional `skill1` + `skill2` replace the bot's existing primary professions. If omitted, existing professions are kept.
- Known non-language skills are normalized to the bot's level cap during `spec`.
- Invalid profiles return a bot-to-master error with valid options.
- Invalid or half-specified profession arguments return a bot-to-master error with valid profession guidance.
- For role profiles mapping to multiple specs, selection is uniform random.
- Gearing, when enabled, runs after talents, spells, profession handling, and skill normalization.

### `gear` flag behavior

- rnd/addclass bot: if `PlayerbotBetterSetup.Spec.AutoGearRndBots = 1`, bot is geared after `spec` even without `gear`.
- altbot: bot is geared only when both are true:
  - `PlayerbotBetterSetup.Spec.AutoGearAltBots = 1`
  - command includes `gear`

### `gearself`

- Commanding player only.
- Requires account level at or above `PlayerbotBetterSetup.GearSelf.MinSecurityLevel`.
- Targets average ilvl approximately to character level.
- Intended for testing/master setup convenience.

### `.specplayer`

- Command for applying this module's spec workflow to a player character by name.
- Requires account level at or above `PlayerbotBetterSetup.SpecPlayer.MinSecurityLevel`.
- If the target is offline and `PlayerbotBetterSetup.SpecPlayer.AllowOfflineQueue = 1`,
  the request is queued and applied automatically at that character's next login.
- Optional `skill1` + `skill2` override primary professions (must provide both).
- Applies level, talents/spec, configurable post-spec maintenance, spell refresh,
  and configurable target-ilvl gearing.
- Sets all known non-language skills to `level * 5` after setup, except Riding:
  level `< 40` clears Riding, `40-69` sets it to `75`, and `70+` sets it to `225`.
- For `.specplayer` at level `60`, Paladins and Warlocks temporarily learn quest-locked class spells during the spell refresh, then have their epic class mount spell removed so Riding can remain at `75` and the quest chain is still available later.
- Most `.specplayer` workflow knobs live under `PlayerbotBetterSetup.SpecPlayer.*`
  in the module config.

## Expansion Source Logic

Used when `AiPlayerbot.LimitTalentsExpansion = 1`.

- `level`: 1-60 Vanilla, 61-70 TBC, 71-80 Wrath.
- `progression`: reads `character_settings` where `source = 'mod-individual-progression'`, but never narrows below the character's own level bracket.
- `auto`: progression first, level fallback if progression data is missing. Progression can widen the cap, but level remains the minimum floor.

Progression tiers are mapped as:

- 0-7: Vanilla
- 8-12: TBC
- 13+: Wrath

## Key Config Options

See `conf/mod-playerbot-bettersetup.conf.dist` for full descriptions.

- `PlayerbotBetterSetup.Spec.Enable`
- `PlayerbotBetterSetup.Spec.RequireMasterControl`
- `PlayerbotBetterSetup.Spec.ShowSpecListOnEmpty`
- `PlayerbotBetterSetup.Spec.AutoGearRndBots`
- `PlayerbotBetterSetup.Spec.AutoGearAltBots`
- `PlayerbotBetterSetup.Spec.ExpansionSource`
- `PlayerbotBetterSetup.Spec.GearModeRndBots`
- `PlayerbotBetterSetup.Spec.GearModeAltBots`
- `PlayerbotBetterSetup.Spec.GearMasterIlvlRatioRndBots`
- `PlayerbotBetterSetup.Spec.GearMasterIlvlRatioAltBots`
- `PlayerbotBetterSetup.Spec.GearValidationLowerRatio`
- `PlayerbotBetterSetup.Spec.GearValidationUpperRatio`
- `PlayerbotBetterSetup.Spec.GearRetryCount`
- `PlayerbotBetterSetup.Spec.GearQualityCapRatioMode`
- `PlayerbotBetterSetup.Spec.GearQualityCapTopForLevel`
- `PlayerbotBetterSetup.SpecPlayer.*`
- `PlayerbotBetterSetup.LoginDiagnostics.Enable`
- `PlayerbotBetterSetup.GearSelf.MinSecurityLevel`

## Requirements

- Required: `mod-playerbots`
- Optional: `mod-individual-progression` (only needed for `ExpansionSource = progression|auto` progression lookup)

## Design Notes

Design and planning details are maintained in:

- `docs/unified-spec-command-design.md`
