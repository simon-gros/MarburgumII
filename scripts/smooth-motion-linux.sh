#!/usr/bin/env bash
# Launch the packaged Linux client with NVIDIA Smooth Motion requested (ADR-029).
#
# Smooth Motion is driver-side frame generation (the NvPresent Vulkan layer); the engine does not
# drive it, so this only asks the driver for it. The client sees the request, refuses native DLSS
# Frame Generation for the session and records external_frame_generation on its MB2RENDER line.
# Whether frames were actually generated stays unknown until presentation evidence (an NvPresent
# log, a frame-time capture) shows it; the client's own frame times remain the renderer's numbers.
#
# Usage: scripts/smooth-motion-linux.sh [--debug-log <file>] [--graphics-queue] [--loader-debug] [--] [client args...]
# MB2_CLIENT overrides the client launcher (default: the staged LinuxClient build).
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
client="${MB2_CLIENT:-$repo_root/Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh}"

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "Smooth Motion through NvPresent is the Linux/Vulkan path; on Windows enable it in the NVIDIA App." >&2
    exit 2
fi
if [[ ! -x "$client" ]]; then
    echo "Missing client launcher: $client (run scripts/package-linux.sh, or set MB2_CLIENT)." >&2
    exit 2
fi
if ! grep -qs '"VK_LAYER_NV_present"' /etc/vulkan/implicit_layer.d/*.json /usr/share/vulkan/implicit_layer.d/*.json \
        /usr/local/share/vulkan/implicit_layer.d/*.json "$HOME"/.local/share/vulkan/implicit_layer.d/*.json; then
    echo "No VK_LAYER_NV_present manifest: this NVIDIA driver does not offer Smooth Motion." >&2
    exit 2
fi

env_args=("NVPRESENT_ENABLE_SMOOTH_MOTION=1")
client_args=()
while (( $# )); do
    case "$1" in
        --debug-log)
            [[ $# -ge 2 ]] || { echo "--debug-log needs a path" >&2; exit 2; }
            log_file="$(realpath -m "$2")"
            mkdir -p "$(dirname "$log_file")"
            env_args+=("NVPRESENT_LOG_LEVEL=4" "NVPRESENT_LOG_FILE=$log_file")
            shift 2
            ;;
        --graphics-queue) env_args+=("NVPRESENT_QUEUE_FAMILY=1"); shift ;;
        --loader-debug) env_args+=("VK_LOADER_DEBUG=layer"); shift ;;
        --) shift; client_args+=("$@"); break ;;
        *) client_args+=("$1"); shift ;;
    esac
done

nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | sed 's/^/GPU: /' >&2 || true
echo "Smooth Motion requested; activation is unverified until NvPresent output confirms it." >&2
exec env "${env_args[@]}" "$client" -vulkan "${client_args[@]}"
