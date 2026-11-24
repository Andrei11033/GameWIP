#!/usr/bin/env bash

# ------------------------------------------------------------
# Managed PowerShell release
# ------------------------------------------------------------

# Pin the official release and both supported Linux archive hashes.
GAMEWIP_POWERSHELL_VERSION='7.7.0'
GAMEWIP_POWERSHELL_X64_SHA256='c89d9c682d2c403dfe726f3df948e359675235a5bd6fab535776de69729ca0cb'
GAMEWIP_POWERSHELL_ARM64_SHA256='e59c89776eaa59790d44b3edd14277435225ab08b6e7603763101b89de83d935'
GAMEWIP_POWERSHELL_COMMAND=''

# ------------------------------------------------------------
# Load managed runtime helpers
# ------------------------------------------------------------

# Load path, discovery, installation, and consent helpers.
source "${BASH_SOURCE[0]%/*}/powershell/Storage.sh"
source "${BASH_SOURCE[0]%/*}/powershell/Discovery.sh"
source "${BASH_SOURCE[0]%/*}/powershell/Install.sh"
source "${BASH_SOURCE[0]%/*}/powershell/Consent.sh"
