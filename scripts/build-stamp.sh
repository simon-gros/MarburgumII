#!/usr/bin/env bash
# The staged builds' stamp (Saved/StagedBuilds/BUILD_STAMP): the commit they were packaged from, whether the
# tree was clean, and the SHA-256 of each executable.
#
#   build-stamp.sh write <config>   after packaging (package-linux.sh does it)
#   build-stamp.sh check            prints the stamp and "build_fresh: yes|NO (...)"; exits 3 when the stamp is
#                                   missing, the executables changed since, or the game's inputs (Source, Config,
#                                   Content, Data, Plugins) differ from the packaged commit or have uncommitted edits
#
# A staged smoke copies the check's output into its summary, so its evidence names the build it ran.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
staged="$repo_root/Saved/StagedBuilds"
stamp="$staged/BUILD_STAMP"
# What the packaged game is built from; a change anywhere here makes an older package stale.
inputs=(Source Config Content Data Plugins MarburgumII.uproject)

executable() {
    local side="$1"
    # Development: MarburgumIIServer; Shipping: MarburgumIIServer-Linux-Shipping.
    find "$staged/Linux$side/MarburgumII/Binaries/Linux" -maxdepth 1 -type f -name "MarburgumII$side*" \
        ! -name '*.debug' ! -name '*.sym' 2>/dev/null | sort | head -n 1
}

dirty() {
    [[ -n "$(git -C "$repo_root" status --porcelain -- "${inputs[@]}")" ]] && echo 1 || echo 0
}

case "${1:-}" in
    write)
        config="${2:?config}"
        tmp="$stamp.tmp"
        {
            echo "git_sha=$(git -C "$repo_root" rev-parse HEAD)"
            echo "dirty=$(dirty)"
            echo "config=$config"
            echo "packaged_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
            for side in Server Client; do
                bin="$(executable "$side")"
                if [[ -z "$bin" ]]; then
                    echo "build-stamp: no staged MarburgumII$side executable under $staged" >&2
                    exit 1
                fi
                echo "sha256_$side=$(sha256sum "$bin" | cut -d' ' -f1)"
            done
        } > "$tmp"
        mv "$tmp" "$stamp"
        cat "$stamp"
        ;;
    check)
        if [[ ! -f "$stamp" ]]; then
            echo "build_fresh: NO (no $stamp; package with scripts/package-linux.sh)"
            exit 3
        fi
        sed 's/^/build_/' "$stamp"
        problems=()
        for side in Server Client; do
            want="$(sed -n "s/^sha256_$side=//p" "$stamp")"
            bin="$(executable "$side")"
            if [[ -z "$bin" ]]; then
                problems+=("$side executable missing")
            elif [[ "$(sha256sum "$bin" | cut -d' ' -f1)" != "$want" ]]; then
                problems+=("$side executable changed since packaging")
            fi
        done
        # A later commit that touches only docs or scripts leaves the build current; one that touches its inputs does not.
        packaged_sha="$(sed -n 's/^git_sha=//p' "$stamp")"
        if [[ "$packaged_sha" != "$(git -C "$repo_root" rev-parse HEAD)" ]]; then
            if ! git -C "$repo_root" cat-file -e "$packaged_sha^{commit}" 2>/dev/null; then
                problems+=("packaged from an unknown commit")
            elif ! git -C "$repo_root" diff --quiet "$packaged_sha" HEAD -- "${inputs[@]}"; then
                problems+=("Source, Config, Content, Data or Plugins changed since the packaged commit")
            fi
        fi
        [[ "$(sed -n 's/^dirty=//p' "$stamp")" == 0 ]] || problems+=("packaged from a dirty tree")
        [[ "$(dirty)" == 0 ]] || problems+=("the tree has uncommitted changes since")
        if (( ${#problems[@]} )); then
            printf 'build_fresh: NO (%s)\n' "$(IFS=';'; echo "${problems[*]}")"
            exit 3
        fi
        echo "build_fresh: yes"
        ;;
    *)
        echo "Usage: $0 write <config> | check" >&2
        exit 2
        ;;
esac
