# Module Rework

## Scope

- Working path: `AzerothDev/modules/mod-playerbot-bettersetup`
- Upstream dependency: `mod-playerbots`
- Constraint for this rework: prefer reusing existing `mod-playerbots` behavior instead of rebuilding overlapping setup logic inside this module.
- This document is the new rework note for the command split described in the prompt. It does not replace upstream behavior; it describes how this module should wrap and narrow it.
- Implementation note: the command split has now been applied. The live command surface is documented in `README.md`; the "Current Module State" section below preserves the pre-rework audit that drove the redesign.

## Current Module State

### Current public commands

- `spec`
- `gearself`
- `.specplayer`

### Current non-command behavior

- Login diagnostics are sent on player login.
- Offline `.specplayer` requests are stored in `character_settings`.
- The module hooks generic chat, whisper, group, guild, and channel chat.
- Selector-compatible fanout already exists for bot chat commands.

### What the current `spec` command actually does

The current bot-side `spec` flow is broader than a spec change:

1. Parse selectors and the `spec <profile> [skill1 skill2] [gear]` grammar.
2. Resolve aliases and role-based random spec buckets.
3. Enforce master-control policy.
4. Sync addclass bot level to the master.
5. Resolve expansion cap from level and optional progression state.
6. Apply talents through a custom filtered premade path.
7. Run post-spec maintenance:
   - glyphs
   - consumables
   - pet init
   - pet talents
8. Learn class/available/quest/special spells.
9. Optionally replace primary professions.
10. Normalize all known non-language skills to level cap.
11. Normalize riding.
12. Optionally autogear with custom master-ilvl logic.
13. Reset bot AI and strategies.

### Current reusable pieces inside this module

- Spec alias and exact-spec resolution.
- Expansion-cap-aware talent application.
- Selector-compatible chat fanout and summary reporting.
- Master-ilvl-based gearing for bots.
- AI reset after command execution.

## Upstream Reuse Map

The new command set should start with `mod-playerbots` behavior first, then add only the module-specific glue that is still missing.

### `setup`

Target behavior from the prompt:

- Use the full `maintenance` flow from `mod-playerbots`.
- Do not assign primary professions.
- Still touch secondary skills.

Best reuse path:

1. `InitAttunementQuests()`
2. `InitBags(false)`
3. `InitAmmo()`
4. `InitFood()`
5. `InitReagents()`
6. `InitConsumables()`
7. `InitPotions()`
8. `InitTalentsTree(true)`
9. `InitPet()`
10. `InitPetTalents()`
11. `InitSkills()`
12. `InitClassSpells()`
13. `InitAvailableSpells()`
14. `InitReputation()`
15. `InitSpecialSpells()`
16. `InitMounts()`
17. `InitGlyphs(false)`
18. `InitKeyring()`
19. `ApplyEnchantAndGemsNew()` when level allows it
20. `DurabilityRepairAll(...)`
21. `SendTalentsInfoData(false)`

Notes:

- This is the exact maintenance order already used upstream and should stay that way.
- Upstream maintenance already does not assign primary professions, which is good for this goal.
- Upstream maintenance does not explicitly cover the full secondary-skill requirement from the prompt:
  - `InitSkills()` covers combat/weapon/armor/riding skills and rogue lockpicking.
  - It does not provide a direct reusable call for first aid, fishing, and cooking without also calling `InitTradeSkills()`, which would assign primary professions too.
- Because only this module should change, `setup` likely needs one module-local secondary-skill helper after the upstream maintenance-shaped pass.
- For alt bots, the safest design is to respect the existing upstream `AiPlayerbot.AltMaintenance*` gates instead of inventing a parallel config layer.

### `spec <spec|role>`

Target behavior from the prompt:

- Apply talents for the requested spec.
- Allow role buckets such as `tank`, `dps`, and `heal`, picking a class-valid spec at random when the role has multiple choices.
- Learn spells.
- Apply enchants and glyphs.
- Refresh hunter pet talents.
- For ClassBots, gear against the master item level.

Best reuse path:

- Keep the current spec alias resolution and class-role mapping.
- Keep the current expansion-cap-aware `ApplySpecTalents(...)`.
- Reuse `LearnSpellsForCurrentLevel(...)`.
- Reuse upstream glyph application via `InitGlyphs(false)`.
- Reuse upstream enchant/gem pass via `ApplyEnchantAndGemsNew()`.
- Reuse upstream pet talent refresh via `InitPetTalents()` for hunter spec updates.
- Reuse the current master-ilvl gear path only for ClassBots.
- Reset AI after the command finishes.

Recommended execution order:

1. Resolve exact spec or role bucket, or show the valid list when the user sends bare `spec`.
2. Apply talents.
3. Learn spells.
4. If target is a ClassBot, apply master-ilvl gear.
5. Apply glyphs.
6. Apply enchants and gems.
7. For hunters, refresh pet talents.
8. Reset AI.

Reason for putting gear before enchants:

- Enchants should land on the final equipment set, not on gear that is about to be replaced.

### `restock`

Target behavior from the prompt:

- Ammo
- pots
- reagents
- food
- consumables
- repair

Best reuse path:

1. `InitAmmo()`
2. `InitFood()`
3. `InitReagents()`
4. `InitConsumables()`
5. `InitPotions()`
6. `DurabilityRepairAll(...)`

Notes:

- `restock` should not touch talents, bags, spells, glyphs, mounts, reputation, or gear generation.
- This is a clean maintenance subset and should stay a clean maintenance subset.

### `pettank <on/off>`

Target behavior from the prompt:

- Toggle pet taunt skills.
- For warlocks, also control which demon should be active for the current spec.
- Default should be off.

Best reuse path:

- Reuse the existing pet autocast mechanism from `mod-playerbots`:
  - `Pet::ToggleAutocast(...)`
  - existing pet-spell toggle helpers already used by bot AI
- Add a module-local taunt detector for hunter and warlock pets rather than building a new generic pet command framework.

Implementation note:

- The allowlist needs to be confirmed during implementation for the supported pet families and demon pets.
- This command should be narrow:
  - `pettank on` enables autocast only for taunt spells.
  - `pettank off` disables autocast only for taunt spells.
  - It should not touch other pet autocast settings.
  - Default state should be off.
  - For warlocks, the preferred demon should also follow the saved `pettank` state.

## What Should Change In This Module

### Target command set

- `setup`
- `spec <spec|role>`
- `restock`
- `pettank <on/off>`

### Command syntax simplification

The new model should remove complexity from the current bot-side `spec` grammar:

- Drop profession arguments from bot-side `spec`.
- Drop the `gear` suffix from bot-side `spec`.
- Keep exact spec aliases and allow class-specific role buckets for `tank`, `dps`, `heal`, `melee`, and `ranged`.
- Keep selector support (`@group2`, `@warrior`, etc.) because it is already useful and already implemented.

This is a major usability win:

- `spec` becomes a pure spec command.
- `spec` with no arguments remains a help/list command showing valid specs for the bot class.
- `setup` becomes the full setup command.
- `restock` becomes the consumable/repair command.
- `pettank` becomes the pet threat control command.

## Problem Areas In The Current Module

### High-level problems

- The module is too broad for its stated purpose.
- Bot setup, spec selection, professions, skill normalization, riding, gearing, login diagnostics, and player-only helpers all live in one module.
- The main implementation is a single large source file with many unrelated responsibilities.
- Old design docs are centered on a unified `spec` workflow, which no longer matches the new desired command split.

### Command-flow problems

- `spec` is overloaded and no longer represents one user intention.
- The current `spec` parser has a complicated grammar:
  - profile
  - optional profession pair
  - optional `gear`
- Role-based randomization adds hidden behavior when the new target design appears to want explicit commands with clearer outcomes.
- The current post-spec maintenance is only a partial copy of upstream maintenance, which creates drift.
- Primary profession reassignment is mixed into a command that should now be spec-only.
- Known-skill and riding normalization are always part of bot `spec`, even though those are not spec concerns.
- Gear behavior differs by bot type and by suffix parsing, which makes the user-facing command harder to reason about.

### Reuse and duplication problems

- The module has a custom autogear pipeline even though upstream already has maintenance/autogear behavior and the module goal is narrower than a full gear subsystem.
- The module has a custom spell-refresh helper, which is useful, but the surrounding command still duplicates setup logic already present in upstream maintenance.
- The new `setup` requirement for secondary skills exposes a gap:
  - upstream maintenance is reusable for almost everything,
  - but secondary professions are not available as a clean upstream-only subset without also assigning primaries.

### Scope problems

- `gearself` is not part of the new target command set.
- `.specplayer` should not block the first bot-command split pass, but it remains an intended follow-up workflow.
- Login diagnostics are not part of the new target command set.

These features should not drive the first rework pass.

## Proposed Rework Shape

### Design principles

- Reuse upstream command behavior and `PlayerbotFactory` flows first.
- Keep selector-compatible chat fanout from the current module.
- Keep exact-spec resolution only where it still directly supports the new command set.
- Avoid new config unless there is no upstream setting that already expresses the policy.
- Keep bot-side commands focused and single-purpose.
- Keep `spec` with no arguments as a discovery/help path.

### Recommended internal structure

Split the current monolith into four command handlers plus shared helpers:

- `HandleSetupCommand(...)`
- `HandleSpecCommand(...)`
- `HandleRestockCommand(...)`
- `HandlePetTankCommand(...)`

Shared helpers that are still worth keeping:

- selector fanout
- master-control enforcement
- exact-spec alias resolution
- expansion-cap resolution for talents
- classbot gear-against-master helper
- AI reset helper

Helpers that should stop being bot-side `spec` concerns:

- primary profession parsing
- bot-side known-skill normalization as part of `spec`
- bot-side riding normalization as part of `spec`

## Phased Rework Plan

### Phase 1: Design and command split

1. Add this rework note and treat it as the new command-split source of truth.
2. Freeze the current broad `spec` design as legacy behavior, not the target state.
3. Define the new parser around four commands only:
   - `setup`
   - `spec`
   - `spec <spec>`
   - `restock`
   - `pettank <on/off>`
4. Keep existing selector and summary infrastructure.

### Phase 2: Rebuild the behavior around upstream reuse

1. Implement `setup` around the upstream maintenance order.
2. Add one module-local helper that actively grants and levels the missing secondary professions:
   - first aid
   - fishing
   - cooking
3. Implement `restock` as the narrow maintenance subset.
4. Narrow `spec` down to:
   - no-arg help/list output
   - talents
   - spells
   - glyphs
   - enchants
   - hunter pet talents
   - classbot-only gear
   - AI reset
5. Implement `pettank` around pet taunt autocast toggling.

### Phase 3: Remove old `spec` baggage

1. Remove profession arguments from bot-side `spec`.
2. Remove `gear` suffix parsing from bot-side `spec`.
3. Keep role-based spec buckets only if they stay class-correct and intentionally random within the bucket.
4. Remove bot-side riding and all-skill normalization from `spec`.
5. Remove partial post-spec maintenance that should now belong to `setup` or `restock`.

### Phase 4: Documentation and config cleanup

1. Update README to describe the new command set.
2. Rewrite config comments to match the reduced command scope.
3. Mark old `spec` design docs as historical or replace them.
4. Remove `gearself`.
5. Leave `.specplayer` in place for now, but document it as a deferred rework item after the new bot commands are stable.
6. Decide separately whether login diagnostics stay or move to a later cleanup pass.

### Deferred follow-up: `.specplayer`

This should not be part of the first command-split delivery, but the intended rework direction is now defined:

1. Accept a target level limited to `60` or `70`.
2. Teach the requested primary professions.
3. Run the new `setup` workflow.
4. Run the new `spec <spec>` workflow.
5. Run the module gear workflow using the configured target ilvl for `.specplayer`.

Notes:

- Keep `.specplayer` available for SOAP-enabled environments.
- Do not start this rework until the new bot-side commands are working and verified.

## Recommended Verification Checklist

- Whisper a single bot:
  - `setup`
  - `spec`
  - `spec frost`
  - `spec tank`
  - `spec dps`
  - `spec heal`
  - `restock`
  - `pettank on`
  - `pettank off`
- Group selector path:
  - `@group2 @hunter restock`
  - `@group2 @hunter pettank off`
  - `@group2 @warrior spec fury`
- Alt bot path:
  - confirm `setup` respects upstream alt-maintenance gates
  - confirm `spec` does not unexpectedly reassign professions
- ClassBot path:
  - confirm `spec <spec>` gears against the master item level
  - confirm enchants are applied after final gear is equipped
- Pet classes:
  - hunter `spec` refreshes pet talents
  - hunter pet taunt toggles correctly
  - warlock `pettank on` picks a tank demon and enables its taunt autocast
  - warlock `pettank off` picks the spec-appropriate dps demon and disables taunt autocast

## Resolved Decisions

- Bot-side `spec` keeps `spec` with no argument as a help/list command showing valid specs for the bot class.
- Hunter `spec` should refresh pet talents.
- `gearself` should be removed.
- `.specplayer` should remain, but its rework is deferred until the new bot-side commands are working.
- The later `.specplayer` workflow should:
  - set the target to level `60` or `70`
  - teach the chosen primary professions
  - run the new `setup`
  - run the new `spec <spec|role>`
  - then apply module-configured `.specplayer` gearing
- `setup` should actively grant missing secondary professions instead of only normalizing already-known ones.

## Recommendation

Build the rework around upstream maintenance semantics, not around the current `spec` monolith.

That gives the cleanest split:

- `setup` = upstream maintenance-shaped full setup
- `spec` = help/list on empty, otherwise exact spec or class-role change plus spell/glyph/enchant refresh, hunter pet talents, warlock pet preference reapply, and classbot gear
- `restock` = consumables/ammo/reagents/repair only
- `pettank` = taunt autocast toggle, plus warlock tank-vs-dps demon preference

Everything else in the current module should be treated as legacy or deferred scope unless it directly supports one of those four commands or the later `.specplayer` follow-up.
