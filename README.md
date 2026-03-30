# mod-playerbot-bettersetup

`mod-playerbot-bettersetup` extends `mod-playerbots` with a split bot setup workflow instead of one overloaded `spec` command.

`setup` follows the upstream maintenance order, but uses the module's configured expansion source for setup-era restrictions. That means setup-side talents, glyphs, and granted secondary profession caps can follow current progression state instead of only raw character level.

The bot-side command set is now:

- `setup`
- `spec`
- `spec <spec|role>`
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

Secondary-profession caps granted by `setup` are expansion-gated:

- Vanilla: up to 300
- TBC: up to 375
- Wrath: up to 450

For addclass/classbots, `setup` also syncs the bot level to the commanding player before running the setup pass.

### `spec`

`spec` with no argument lists the exact specs and role buckets available for the bot class.

`spec <spec|role>` is now a narrow spec command:

- applies talents for the requested exact spec, or for a random valid spec within the requested role bucket
- learns class, trainer, quest, and special spells
- gears addclass/classbots against the master's ilvl policy when enabled
- applies glyphs
- applies enchants and gems
- refreshes hunter pet and pet talents
- reapplies the saved warlock pet preference after the spec change
- resets bot AI

Notes:

- Bot-side `spec` no longer accepts profession arguments.
- Bot-side `spec` no longer accepts a trailing `gear` flag.
- Role buckets such as `tank`, `dps`, `heal`, `melee`, and `ranged` resolve per class. If a class has several valid specs in the bucket, one is chosen at random.
- Exact spec aliases such as `prot`, `disc`, `mm`, `blood tank`, and `feral dps` still work.

### `restock`

Runs the narrow consumable and repair pass:

- ammo
- food
- reagents
- consumables
- potions
- repair

### `pettank <on|off>`

Controls pet taunt behavior, and for warlocks also controls which demon the bot keeps active.

- For hunter and other pet classes, `pettank on` enables taunt autocast on the active pet and `pettank off` disables it.
- For warlocks, `pettank` is a saved preference:
  - demonology keeps a felguard whether `pettank` is on or off
  - demonology with `pettank on` enables `Anguish`; `pettank off` disables it
  - affliction and destruction with `pettank off` prefer their dps demons
  - affliction and destruction with `pettank on` switch to a voidwalker and enable `Torment`

`setup` and hunter `spec` force pet taunt back to off so the module default stays off.

## Examples

- `setup`
- `spec`
- `spec frost`
- `spec tank`
- `spec dps`
- `spec heal`
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
