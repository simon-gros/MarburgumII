# Windows twin of smooth-motion-linux.sh: launch the packaged client with NVIDIA Smooth Motion requested (ADR-029).
#
# Smooth Motion through the NvPresent Vulkan layer (NVPRESENT_* variables, VK_LAYER_NV_present) is the Linux driver's
# path; the Windows driver offers Smooth Motion only as a driver setting in the NVIDIA App, which a launcher cannot
# request. smooth-motion-linux.sh refuses to run on anything but Linux (uname -s) with exit 2 and this advice, so this
# twin does the same: it keeps the step, says SKIPPED with the reason, and exits 2. The engine never drives Smooth
# Motion itself; with it enabled in the NVIDIA App, start the client normally (scripts\mb2.ps1 or the staged
# WindowsClient build) and the client records what it sees on its MB2RENDER line.
#
# Usage: scripts\smooth-motion-windows.ps1 [--debug-log <file>] [--graphics-queue] [--loader-debug] [--] [client args...]
# MB2_CLIENT overrides the client launcher (default: the staged WindowsClient build).
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib\MarburgumSmoke.psm1') -Force -DisableNameChecking

# [[ "$(uname -s)" != "Linux" ]]: always true here.
Exit-Usage 'SKIPPED (Smooth Motion through NvPresent is the Linux/Vulkan path; on Windows enable it in the NVIDIA App.)'
