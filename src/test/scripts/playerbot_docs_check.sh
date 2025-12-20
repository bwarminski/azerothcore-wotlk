#!/usr/bin/env bash
# ABOUTME: Validates playerbot XP upgrade documentation is present and current.
# ABOUTME: Ensures changelog and README mention required config keys.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CHANGELOG_PATH="${ROOT_DIR}/doc/CHANGELOG-playerbots-xp-upgrade.md"
README_PATH="${ROOT_DIR}/modules/mod-playerbots/README.md"

if [[ ! -f "${CHANGELOG_PATH}" ]]; then
  echo "Missing changelog: ${CHANGELOG_PATH}" >&2
  exit 1
fi

if [[ ! -f "${README_PATH}" ]]; then
  echo "Missing README: ${README_PATH}" >&2
  exit 1
fi

required_changelog_entries=(
  "AiPlayerbot.XpUpgradeEnabled"
  "AiPlayerbot.XpUpgradeChunk"
  "AiPlayerbot.VendorSeedEnabled"
  "AiPlayerbot.ProgressionState"
  "AiPlayerbot.BiSWeeksAtEndgame"
  "AutoUpgradeEquip"
)

for entry in "${required_changelog_entries[@]}"; do
  if ! rg -q --fixed-strings "${entry}" "${CHANGELOG_PATH}"; then
    echo "Missing changelog entry: ${entry}" >&2
    exit 1
  fi
done

if ! rg -q --fixed-strings "AiPlayerbot.XpUpgradeEnabled" "${README_PATH}"; then
  echo "README missing AiPlayerbot.XpUpgradeEnabled mention" >&2
  exit 1
fi
