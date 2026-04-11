# mod-playerbot-bettersetup

`mod-playerbot-bettersetup` extends `mod-playerbots` with a split bot setup workflow instead of one overloaded `spec` command.

`setup` and `spec` use the module's configured expansion source for talent, glyph, and profession-era restrictions. When `PlayerbotBetterSetup.Spec.ExpansionSource` is `progression` or `auto`, the module reads `mod-individual-progression` from the target bot or player itself and falls back to level brackets only when progression data is missing.

The bot-side command set is now:

- `setup`
- `spec`
- `spec <spec|role>`
- `restock`
- `petspec <tank|dps|stealth|control>`

Selectors from `mod-playerbots` still work in whisper, group, guild, and channel chat.

## Bot Commands

### `setup`

Runs the upstream `maintenance` flow in maintenance order, with module-local filtering so primary professions are not taught or reassigned.

What it does:

- attunement quests
- bags
- ammo
- food
- reagents
- consumables
- potions
- talents
- pet and pet talents
- combat and class skills
- missing secondary professions: first aid, fishing, cooking
- class trainer spells
- secondary-profession trainer spells
- reputation
- special spells
- mounts
- glyphs
- keyring
- enchants and gems when level allows
- repair

Secondary-profession caps granted by `setup` are expansion-gated:

- Vanilla: up to 300
- TBC: up to 375
- Wrath: up to 450

For addclass/classbots, `setup` also syncs the bot level to the commanding player before running the setup pass.

For hunters with a saved `petspec`, `setup` preserves the current active pet when it already matches that petspec, reapplies its talents and taunt state, and only swaps or creates a pet when the current one does not fit.

For paladins, `setup` removes `Righteous Fury` when the current spec is not protection.

### `spec`

`spec` with no argument lists the exact specs and role buckets available for the bot class.

`spec <spec|role>` is now a narrow spec command:

- applies talents for the requested exact spec, or for a random valid spec within the requested role bucket
- learns class, trainer, quest, and special spells
- gears addclass/classbots against the master's ilvl policy when enabled
- applies glyphs
- applies enchants and gems
- reapplies saved hunter and warlock pet preferences after the spec change
- removes `Righteous Fury` from non-protection paladins after the spec change
- resets bot AI

Notes:

- Bot-side `spec` no longer accepts profession arguments.
- Bot-side `spec` no longer accepts a trailing `gear` flag.
- Role buckets such as `tank`, `dps`, `heal`, `melee`, and `ranged` resolve per class. If a class has several valid specs in the bucket, one is chosen at random.
- Exact spec aliases such as `prot`, `disc`, `mm`, `blood tank`, and `feral dps` still work.
- Expansion limits for `spec` come from the target bot's own progression row when available, not from the commanding player.

### `restock`

Runs the narrow consumable and repair pass:

- ammo
- food
- reagents
- consumables
- potions
- repair

### `petspec <tank|dps|stealth|control>`

Controls pet family or demon choice plus taunt autocast behavior for pet classes. `petspec` is only available to hunters and warlocks, and the chosen value is saved and reapplied by `setup` and `spec`.

If the current active pet or demon already satisfies the requested `petspec`, the module keeps it and only reapplies talents and taunt-autocast state unless a higher-priority valid demon is available for that warlock petspec.

For hunter altbots, `petspec` only uses pets they already own. It prefers a matching active or stabled pet when one exists, swaps to that owned pet if needed, and never grants a brand-new family just to satisfy the requested role.

- Hunters:
  - `petspec tank` tames a tenacity pet, preferring bear-style families, and enables taunt autocast
  - `petspec dps` tames a ferocity pet, preferring wolves, and disables taunt autocast
  - `petspec stealth` tames a ferocity pet, preferring cats, and disables taunt autocast
  - `petspec control` tames a cunning pet, preferring bird-of-prey families, and disables taunt autocast
- Warlocks:
  - `petspec tank` uses felguard if available or voidwalker as fallback, with taunt autocast on
  - `petspec dps` uses demonology = felguard or imp, affliction = succubus or imp, destruction = imp, with taunt autocast off
  - `petspec stealth` uses affliction or demonology = succubus or imp, destruction = imp
  - `petspec control` uses felhunter or succubus or imp, in that order

If no explicit hunter `petspec` has been saved, `setup` and `spec` still force hunter taunt autocast back to off.

## Examples

- `setup`
- `spec`
- `spec frost`
- `spec tank`
- `spec dps`
- `spec heal`
- `spec blood tank`
- `restock`
- `petspec tank`
- `petspec control`
- `@group2 @hunter restock`
- `@group2 @warrior spec fury`

## `.specplayer`

`.specplayer` is still available, including offline queue support, but it remains the legacy workflow for now. Its larger rework is intentionally deferred until the bot-side commands are settled.

## Key Config Notes

See `conf/mod-playerbot-bettersetup.conf.dist` for the full list.

- `PlayerbotBetterSetup.Spec.Enable`
- `PlayerbotBetterSetup.Spec.RequireMasterControl`
- `PlayerbotBetterSetup.Spec.ShowSpecListOnEmpty`
- `PlayerbotBetterSetup.Spec.AutoGearRndBots`
- `PlayerbotBetterSetup.Spec.GearModeRndBots`
- `PlayerbotBetterSetup.Spec.GearMasterIlvlRatioRndBots`
- `PlayerbotBetterSetup.Spec.GearValidationLowerRatio`
- `PlayerbotBetterSetup.Spec.GearValidationUpperRatio`
- `PlayerbotBetterSetup.Spec.GearRetryCount`
- `PlayerbotBetterSetup.Spec.GearQualityCapRatioMode`
- `PlayerbotBetterSetup.Spec.GearQualityCapTopForLevel`
- `PlayerbotBetterSetup.Spec.ExpansionSource`
- `PlayerbotBetterSetup.SpecPlayer.*`
- `PlayerbotBetterSetup.LoginDiagnostics.Enable`

## Requirements

- Required: `mod-playerbots`
- Optional: `mod-individual-progression` for `PlayerbotBetterSetup.Spec.ExpansionSource = progression|auto`

## Design Notes

Current rework notes live in:

- `module-rework.md`
