# `specplayer` GM Command Implementation Plan

Date: March 6, 2026  
Status: Planning only (no code changes yet)  
Module: `mod-playerbot-bettersetup`

## 1. Goal

Add a GM command that applies the module's spec workflow to a named player character, including leveling and gear pass.

Requested flow example:

- `.specplayer Farsong marksman 60`

Expected result:

1. Set `Farsong` to level 60.
2. Apply `marksman` spec talents.
3. Run gear setup using the same behavior as `gearself`.

## 2. Proposed Command Contract

Primary syntax:

- `.specplayer <playerName> <specProfile> <level>`

Where:

- `<playerName>`: character name (normalized name lookup).
- `<specProfile>`: any supported exact spec/alias already handled by this module (`marksman`, `mm`, `prot`, etc.).
- `<level>`: numeric target level (clamped to server max level).

Initial scope:

- Online targets only (required for direct talent + gear operations).
- GM-only command (`SEC_GAMEMASTER`).

## 3. Execution Semantics

For a valid command:

1. Resolve target player by name and require connected player.
2. Validate caller security and target security (`HasLowerSecurity` guard).
3. Validate/normalize requested level.
4. Set target level and reset XP (`GiveLevel`, `InitTalentForLevel`, `SetUInt32Value(PLAYER_XP, 0)`).
5. Resolve requested spec via existing resolver:
   - `ResolveRequestedSpec(...)`
   - `FindSpecNoForDefinition(...)`
6. Apply talents with existing module logic:
   - `ApplySpecTalents(...)`
   - expansion cap resolved for the target context.
7. Run post-spec maintenance and spell learning:
   - `RunPostSpecMaintenance(...)`
   - `LearnSpellsForCurrentLevel(...)`
8. Run `gearself`-equivalent gearing on the target:
   - `ApplyGearSelf(targetPlayer)`
9. Send clear GM feedback (`success`, `invalid spec`, `player not online`, etc.).

## 4. Code Touchpoints

Planned files:

- `src/PlayerbotBetterSetup.cpp`
  - Add command registration (`CommandScript`) for `.specplayer`.
  - Add argument parser/helper for `<name> <spec> <level>`.
  - Add target workflow helper reusing existing internal spec/gear functions.
  - Keep command script in the same file to reuse internal static helpers safely.
  - Instantiate command script in `AddPlayerbotBetterSetupScripts()`.
- `README.md`
  - Document `.specplayer` usage and constraints.

No config changes planned for v1.

## 5. Guardrails and Error Handling

Cases to handle explicitly:

- Missing args -> print usage.
- Target name not found / target offline -> fail with message.
- Invalid level value -> fail with message.
- Invalid spec for target class -> fail with class-specific valid list.
- No matching premade template for resolved spec -> fail with message.
- Security mismatch (`HasLowerSecurity`) -> reject operation.

## 6. Validation Plan

Manual tests after implementation:

1. Success case:
   - `.specplayer Farsong marksman 60`
   - Confirm level 60, marksman talents present, gear refreshed.
2. Alias case:
   - `.specplayer Farsong mm 60`
3. Invalid spec for class:
   - `.specplayer Farsong holy 60` on a hunter -> proper error.
4. Offline target:
   - Command rejects with clear message.
5. Invalid level:
   - `.specplayer Farsong marksman abc` -> usage/error.
6. Security check:
   - Lower-privileged GM cannot modify higher-privileged account character.

## 7. Risks and Mitigations

- Risk: player-targeted post-spec routines were originally bot-focused.
  - Mitigation: only reuse helpers that are player-safe; avoid bot-AI-only calls.
- Risk: expansion cap source tied to command sender in current logic.
  - Mitigation: for `.specplayer`, resolve cap using target player context to avoid GM progression data affecting result.
- Risk: name parsing ambiguity.
  - Mitigation: use command parser + normalized character lookup and return exact usage on failure.
