# Unified `spec` Command Design (Draft v0.4)

Date: February 24, 2026
Status: Draft (discussion only, no code changes yet)
Module: `mod-playerbot-bettersetup`

## 1. Problem Statement

Current playerbot behavior uses different paths for bot setup:

- `init=auto` style setup (used for random/addclass setup flows) applies full initialization behavior, including talent-limit behavior in the template path.
- `talents spec <specName>` (chat action path) changes talents for a bot but does not use the same setup behavior and currently does not apply the expansion-row limit in `InitTalentsBySpecNo` / `InitTalentsByParsedSpecLink`.

You want one unified user command for both rndbots and altbots:

- Command: `spec`
- Example with selectors: `@group2 @warrior spec tank`
- Works in whisper/group/raid/channel command flow like existing playerbots chat commands.
- Applies to any targeted bots, including altbots.

## 2. Goals

- Provide one command family: `spec ...`.
- Keep existing selector behavior (`@group2`, `@warrior`, etc.) as users already use in playerbots.
- Make behavior uniform across rndbots and altbots for spec assignment.
- Avoid full `Randomize` behavior in this flow.
- Autogear policy by bot type:
  - Rndbots/addclass bots are autogeared after `spec` when `AutoGearRndBots = 1` (no `gear` keyword required).
  - Altbots are autogeared only when both are true:
    - command explicitly includes `gear`
    - `AutoGearAltBots = 1`
- Support two post-spec gear modes, with separate config per bot type:
  - `top_for_level`
  - `master_ilvl_ratio` (e.g. `1.0`, `0.9`)
- Make talent/glyph expansion limiting support two modes:
  - Existing level-based behavior (`AiPlayerbot.LimitTalentsExpansion`).
  - Progression-state-based behavior (from `mod-individual-progression`).
- Keep this extension design additive and low-risk.

## 3. Non-Goals (for first iteration)

- Replacing all existing `talents ...` commands.
- Building a full custom talent-tree editor.
- Re-running full `PlayerbotFactory::Randomize` from the new `spec` command.

## 4. Proposed User-Facing Command

### 4.1 Base syntax

- `spec`
- `spec <role_or_exact_spec>`
- `spec <role_or_exact_spec> gear`

### 4.2 Selector-compatible syntax

- `<selectors> spec <profile_or_exact_spec> [gear]`
- Example: `@group2 @warrior spec tank`
- Example with explicit gear request (for altbots): `@group2 @warrior spec dps gear`

### 4.3 Behavior by argument

- `spec` (no args):
  - Returns valid role/spec options for the targeted bot.
  - Example for hunter: `dps (random beastmaster/marksman/survival), beastmaster, marksman, survival`.

- `spec <role>`:
  - Role-level command that may randomize between multiple valid specs for that role and class.
  - Example: rogue `spec melee` randomly chooses assassination/combat/subtlety.

- `spec <exact_spec>`:
  - Deterministic specific spec selection.
  - Example: rogue `spec combat`.
  - Accept both full and abbreviated names (example: `protection` or `prot`, `marksman` or `mm`).

- `spec <role_or_exact_spec> gear`:
  - Explicitly requests post-spec autogear.
  - Required for altbot autogear.
  - Not required for rndbots/addclass bots; they follow `AutoGearRndBots`.

### 4.4 Initial role vocabulary

- `tank`
- `heal`
- `dps`
- `melee`
- `ranged`

## 5. Command Semantics

For each bot matching selectors:

1. Determine bot type:
- rndbot/addclass-style bot
- altbot (player-owned bot)

2. Resolve input as:
- explicit spec (deterministic), or
- role profile (random among eligible specs for that role/class).

3. Apply talents through spec-based flow (equivalent intent to `talents spec`, not `Randomize`).

4. Enforce expansion talent limits when enabled:
- Keep support for `AiPlayerbot.LimitTalentsExpansion`.
- Use selected expansion-source model (level vs progression-state; see section 7.3).

5. Post-spec refresh behavior:
- refresh glyphs
- refresh consumables
- refresh pet talents
- run maintenance-level spell refresh (learned/available spells)
- autogear decision:
  - rndbots/addclass bots: autogear when `AutoGearRndBots=1`.
  - altbots: autogear only if command has `gear` and `AutoGearAltBots=1`.
- autogear mode uses per-type gear policy:
  - `top_for_level`: best available gear for character level.
  - `master_ilvl_ratio`: target cap based on commanding player's effective item level times ratio.

6. Error/reporting behavior:
- Invalid spec/role for a bot is reported as whisper feedback to master.
- Command summary should include matched/updated/failed counts.

## 6. Spec Resolution Rules (Draft)

This command supports both broad and specific targeting.

- Broad role input can randomize across multiple matching specs.
- Specific input maps to one exact spec.

Phase 1 role-to-spec category map:

- Warrior:
  - `tank` -> `protection`
  - `melee` -> `arms`, `fury`
  - `dps` -> `arms`, `fury`
  - exact specs -> `arms`, `fury`, `protection`
- Paladin:
  - `tank` -> `protection`
  - `heal` -> `holy`
  - `melee` -> `retribution`
  - `dps` -> `retribution`
  - exact specs -> `holy`, `protection`, `retribution`
- Druid:
  - `tank` -> `feral_tank`
  - `heal` -> `restoration`
  - `melee` -> `feral_dps`
  - `ranged` -> `balance`
  - `dps` -> `balance`, `feral_dps`
  - exact specs -> `balance`, `feral_tank`, `feral_dps`, `restoration`
- Death Knight:
  - `tank` -> `blood_tank`
  - `melee` -> `blood_dps`, `frost`, `unholy`
  - `dps` -> `blood_dps`, `frost`, `unholy`
  - exact specs -> `blood_tank`, `blood_dps`, `frost`, `unholy`
- Priest:
  - `heal` -> `discipline`, `holy`
  - `ranged` -> `shadow`
  - `dps` -> `shadow`
  - exact specs -> `discipline`, `holy`, `shadow`
- Shaman:
  - `heal` -> `restoration`
  - `melee` -> `enhancement`
  - `ranged` -> `elemental`
  - `dps` -> `elemental`, `enhancement`
  - exact specs -> `elemental`, `enhancement`, `restoration`
- Rogue:
  - `melee` -> `assassination`, `combat`, `subtlety`
  - `dps` -> `assassination`, `combat`, `subtlety`
  - exact specs -> `assassination`, `combat`, `subtlety`
- Hunter:
  - `ranged` -> `beastmaster`, `marksman`, `survival`
  - `dps` -> `beastmaster`, `marksman`, `survival`
  - exact specs -> `beastmaster`, `marksman`, `survival`
- Mage:
  - `ranged` -> `arcane`, `fire`, `frost`
  - `dps` -> `arcane`, `fire`, `frost`
  - exact specs -> `arcane`, `fire`, `frost`
- Warlock:
  - `ranged` -> `affliction`, `demonology`, `destruction`
  - `dps` -> `affliction`, `demonology`, `destruction`
  - exact specs -> `affliction`, `demonology`, `destruction`

If a requested role has no valid class mapping (example: `heal` on warrior), return a clear rejection for that bot.

Alias policy:

- Each supported exact spec should accept:
  - one full-word name (example: `protection`)
  - one short alias (example: `prot`)
- This applies consistently across classes (example: `marksman`/`mm`, `beastmaster`/`bm`, etc.).

## 7. Configuration (Module)

Proposed keys in `mod-playerbot-bettersetup.conf.dist`:

- `PlayerbotBetterSetup.Spec.Enable = 1`
- `PlayerbotBetterSetup.Spec.AutoGearRndBots = 1`
- `PlayerbotBetterSetup.Spec.AutoGearAltBots = 0`
- `PlayerbotBetterSetup.Spec.RequireMasterControl = 1`
- `PlayerbotBetterSetup.Spec.ShowSpecListOnEmpty = 1`
- `PlayerbotBetterSetup.Spec.RoleRandomMode = uniform`
- `PlayerbotBetterSetup.Spec.ExpansionSource = auto`
  - `level`: use only level brackets (existing `AiPlayerbot.LimitTalentsExpansion` semantics)
  - `progression`: use `mod-individual-progression` progression state brackets
  - `auto`: progression when available, else level
- `PlayerbotBetterSetup.Spec.GearModeRndBots = master_ilvl_ratio`
  - `top_for_level`
  - `master_ilvl_ratio`
- `PlayerbotBetterSetup.Spec.GearModeAltBots = master_ilvl_ratio`
  - `top_for_level`
  - `master_ilvl_ratio`
- `PlayerbotBetterSetup.Spec.GearMasterIlvlRatioRndBots = 1.0`
- `PlayerbotBetterSetup.Spec.GearMasterIlvlRatioAltBots = 1.0`

### 7.1 Gear policy decision

- `spec` command must not call full randomize behavior.
- Rndbots/addclass bots are autogeared when `PlayerbotBetterSetup.Spec.AutoGearRndBots = 1`.
- Altbots should be autogeared only when:
  - command includes `gear` (example: `spec dps gear`), and
  - `PlayerbotBetterSetup.Spec.AutoGearAltBots = 1`.
- This design explicitly addresses the mismatch where current playerbots `autogear` behavior does not mirror `init=auto` style item-level targeting.
- New gear policy allows:
  - `top_for_level` for max level-appropriate gear, or
  - `master_ilvl_ratio` to cap bot gear against master item-level (e.g. `1.0`, `0.9`).
- Separate enable flags and separate ratio values exist for rndbots and altbots.

Behavior examples:

- rndbot with `AutoGearRndBots=1`: `spec tank` => geared
- rndbot with `AutoGearRndBots=0`: `spec tank` => not geared
- altbot with `AutoGearAltBots=1`: `spec tank` => not geared
- altbot with `AutoGearAltBots=1`: `spec tank gear` => geared
- altbot with `AutoGearAltBots=0`: `spec tank` or `spec tank gear` => not geared

### 7.2 Gear item-level policy details

When `GearMode*=master_ilvl_ratio`:

- Compute target gear cap from command sender (master) effective item level.
- Apply per-type ratio:
  - rndbots: `GearMasterIlvlRatioRndBots`
  - altbots: `GearMasterIlvlRatioAltBots`
- Example:
  - ratio `1.0`: cap at master's effective level.
  - ratio `0.9`: cap at 90% of master's effective level.

When no valid master context exists for ratio mode (edge/admin cases), fallback is always `top_for_level`.

### 7.3 Expansion-source model for talent limits

Current playerbots logic uses level thresholds for expansion emulation when `AiPlayerbot.LimitTalentsExpansion = 1`.

This module design adds progression-tier support:

- Progression availability check is done on `character_settings` using:
  - `guid = <master guid>`
  - `source = 'mod-individual-progression'`
- If that row exists, the progression tier is read from the `data` column (first value).
- Tier ranges (from `ProgressionState` enum):
  - Vanilla: `0..7`
  - TBC: `8..12`
  - Wrath: `13..18`

When `ExpansionSource=progression`:

- Limit talents/glyphs by progression bracket instead of character level.
- If no progression row exists for the master, fallback to level-based logic.

When `ExpansionSource=auto`:

- If `character_settings` has `source='mod-individual-progression'` for the master guid, use progression.
- Otherwise fallback to existing level-based logic.

Role randomization policy:

- `RoleRandomMode` is `uniform` for v0.4 (equal chance per eligible spec).
- Example: rogue `spec dps` / `spec melee` => 33.3% each for assassination/combat/subtlety.

## 8. Behavioral Notes

- Selector support should match current playerbots selector behavior as closely as possible.
- The new `spec` command is an addition, not a replacement, for existing `talents` flows.
- On invalid input, send clear whisper feedback to master.
- `spec` without args should return valid options for the receiving bot/class.
- Command feedback should include matched/updated/failed counts.

## 9. Safety and Permissions

- Respect existing bot command permission/security checks.
- Respect ownership/master restrictions where applicable.
- Never trigger full randomize setup from this feature.
- Do not run altbot autogear unless both conditions are true:
  - command includes `gear`
  - `PlayerbotBetterSetup.Spec.AutoGearAltBots = 1`

## 10. Implementation Strategy (Planned, no code yet)

Phase 1:
- Add `spec` command handling path.
- Implement broad-role + exact-spec parsing.
- Implement exact-spec alias pairs (full word + abbreviation) for all supported specs.
- Add per-class option listing for empty `spec`.
- Apply talents with expansion-limit parity (`LimitTalentsExpansion`) in both paths.
- Add expansion-source selection (`level|progression|auto`).
- Add refresh sequence: glyphs, consumables, pet talents, spell maintenance.
- Implement uniform random weighting for role-based spec picks.
- Ship the full per-class role-to-spec category map in Phase 1.
- Add post-spec autogear policy:
  - rndbots autogear path gated by `AutoGearRndBots`
  - altbot `gear` argument gate + config gate
  - per-type gear mode (`top_for_level` vs `master_ilvl_ratio`)
  - per-type ilvl ratios

Phase 2:
- Add richer per-bot result diagnostics.

## 11. Decisions Locked

1. Support full-word and abbreviation aliases for exact specs (example: `protection`/`prot`, `marksman`/`mm`).
2. Use uniform weighting for randomized role picks.
3. Ship the full per-class role-to-spec category map in Phase 1.

---

This draft captures requested behavior and is intended for review before implementation.
