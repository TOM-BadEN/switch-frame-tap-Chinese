#!/usr/bin/env bash
# Validate the nine browser-downloaded packages, then install local files.
# Argument 1 is their directory; defaults to the script's own directory.
# sudo may request the user's password. No package downloads are initiated here.
set -euo pipefail
# Absolute directory containing the trusted package manifest.
g_scriptDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# Directory holding the unmodified downloaded archives.
g_packageDir=${1:-$g_scriptDir}
cd "$g_packageDir"
# Validate completeness and content before requesting administrative access.
/usr/bin/shasum -a 256 -c "$g_scriptDir/SHA256SUMS"
# Exact local archive paths; do not include unrelated files or signatures.
g_packages=()
# Manifest fields: expected digest and archive filename for each dependency.
while read -r g_digest g_filename; do
    g_packages+=("$PWD/$g_filename")
done < "$g_scriptDir/SHA256SUMS"
sudo dkp-pacman -U --needed "${g_packages[@]}"
