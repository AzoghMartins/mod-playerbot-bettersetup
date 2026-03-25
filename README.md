# mod-playerbot-bettersetup

`mod-playerbot-bettersetup` extends `mod-playerbots` with a split bot setup workflow instead of one overloaded `spec` command.

The bot-side command set is now:

- `setup`
- `spec`
- `spec <spec>`
- `restock`
- `pettank <on|off>`

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

For addclass/classbots, `setup` also syncs the bot level to the commanding player before running the setup pass.

### `spec`

`spec` with no argument lists the exact specs available for the bot class.

`spec <spec>` is now a narrow spec command:

- applies talents for the requested exact spec
- learns class, trainer, quest, and special spells
- gears addclass/classbots against the master's ilvl policy when enabled
- applies glyphs
- applies enchants and gems
- refreshes hunter pet and pet talents
- resets bot AI

Notes:

- Bot-side `spec` no longer accepts profession arguments.
- Bot-side `spec` no longer accepts a trailing `gear` flag.
- Bot-side `spec` no longer uses role-random buckets such as `tank` or `dps`; use an exact spec name or alias such as `prot`, `disc`, `mm`, `blood tank`, `feral dps`, and so on.

### `restock`

Runs the narrow consumable and repair pass:

- ammo
- food
- reagents
- consumables
- potions
- repair

### `pettank <on|off>`

Toggles pet taunt autocast only.

- `pettank on` enables taunt autocast on the active pet
- `pettank off` disables taunt autocast on the active pet

`setup` and hunter `spec` force pet taunt back to off so the module default stays off.

## Examples

- `setup`
- `spec`
- `spec frost`
- `spec blood tank`
- `restock`
- `pettank on`
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
