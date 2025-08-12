#!/bin/sh
# Convert paths but preserve quoting
args=()
for arg in "$@"; do
    case "$arg" in
        /c/*) arg="C:${arg#/c/}" ;;
        /d/*) arg="D:${arg#/d/}" ;;
        /e/*) arg="E:${arg#/e/}" ;;
        # Handle vcpkg-specific path patterns
        *vcpkg_installed*) arg=$(echo "$arg" | sed 's|/\([a-z]\)/|\U\1:/|g') ;;
    esac

    # Normalize path separators
    arg=$(echo "$arg" | tr '/' '\\')

    args+=("$arg")
done
exec /usr/bin/install "${args[@]}"
