#!/bin/sh
# Convert paths but preserve quoting
args=()
for arg in "$@"; do
    case "$arg" in
        /c/*) arg="C:${arg#/c/}" ;;
        /d/*) arg="D:${arg#/d/}" ;;
        /e/*) arg="E:${arg#/e/}" ;;
        # Special handling for vcpkg paths
        *vcpkg_installed*) arg=$(echo "$arg" | sed 's|/\([a-z]\)/|\U\1:/|g') ;;
    esac
    args+=("$arg")
done
exec /usr/bin/make "${args[@]}"
