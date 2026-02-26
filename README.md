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
- Post-spec maintenance for glyphs, consumables, pets, pet talents, and spell refresh.
- Optional login diagnostics in chat.
- Configurable-security `gearself` helper command for test gearing.

## Commands

Use normal playerbot chat channels and prefixes.

- `spec`
- `spec <profile>`
- `spec <profile> gear`
- `gearself` (minimum account level is configurable)

Examples:

- `spec`
- `spec tank`
- `spec holy`
- `spec prot`
- `spec dps gear`
- `@group2 @warrior spec tank`
- `@group2 @warrior spec dps gear`

### `spec` behavior

- `spec` with no argument returns valid role/spec options for the target bot class.
- `<profile>` can be a role or an exact spec alias.
- Invalid profiles return a bot-to-master error with valid options.
- For role profiles mapping to multiple specs, selection is uniform random.

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

## Expansion Source Logic

Used when `AiPlayerbot.LimitTalentsExpansion = 1`.

- `level`: 1-60 Vanilla, 61-70 TBC, 71-80 Wrath.
- `progression`: reads `character_settings` where `source = 'mod-individual-progression'`.
- `auto`: progression first, level fallback if progression data is missing.

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
- `PlayerbotBetterSetup.LoginDiagnostics.Enable`
- `PlayerbotBetterSetup.GearSelf.MinSecurityLevel`

## Requirements

- Required: `mod-playerbots`
- Optional: `mod-individual-progression` (only needed for `ExpansionSource = progression|auto` progression lookup)

## Design Notes

Design and planning details are maintained in:

- `docs/unified-spec-command-design.md`
