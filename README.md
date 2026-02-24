# mod-playerbot-bettersetup

`mod-playerbot-bettersetup` is an extension module for `mod-playerbots` focused on bot setup quality-of-life.

It introduces a unified `spec` command that works for both rndbots/addclass bots and altbots, with selector support (`@group2`, `@warrior`, etc.), role-based or exact-spec targeting, and consistent post-spec setup behavior.

## What this module does

- Unifies spec assignment into one command family: `spec`.
- Supports role-based picks (`tank`, `heal`, `dps`, `melee`, `ranged`) and exact specs (`prot`, `mm`, etc.).
- Applies one consistent spec flow for both rndbots and altbots.
- Adds configurable post-spec autogear policy by bot type.
- Supports expansion-limit behavior based on:
  - level-based playerbots logic, or
  - progression tiers from `mod-individual-progression`.

## How to use

Send commands the same way you already command playerbots (whisper, party, raid, channel), including selectors.

Examples:

- `spec`
- `spec tank`
- `spec combat`
- `spec dps gear`
- `@group2 @warrior spec tank`
- `@group2 @warrior spec dps gear`

Autogear behavior:

- rndbot/addclass bots: geared on `spec ...` when `AutoGearRndBots=1`.
- altbots: geared only if command includes `gear` and `AutoGearAltBots=1`.

## Requirements

- `mod-playerbots` (required)
- `mod-individual-progression` (optional, only for progression-based expansion source)

## Status

Design is documented in:

- `docs/unified-spec-command-design.md`

Implementation is in progress.
