#!/bin/bash
# *****************************************************************************
# * Make Offload.app self-contained.                                          *
# *                                                                           *
# * The engine is a thin layer over wxString (zen's Zstring) and glib, so the *
# * binary links Homebrew's wx, glib, gettext, pcre2 and LLVM libc++ by       *
# * absolute path. That is fine on the machine that built it and useless      *
# * anywhere else. This copies every non-system dylib into                    *
# * Contents/Frameworks, rewrites the load commands to @rpath, and re-signs.  *
# *                                                                           *
# *   ./bundle-dylibs.sh Offload.app                                          *
# *                                                                           *
# * bash 3.2 on purpose: that is what macOS ships, and it has no associative  *
# * arrays, so the two mappings are tab-separated files.                      *
# *****************************************************************************
set -euo pipefail

APP="${1:-Offload.app}"
BIN="$APP/Contents/MacOS/Offload"
FW="$APP/Contents/Frameworks"

[ -x "$BIN" ] || { echo "no executable at $BIN" >&2; exit 1; }
[ "$(uname -s)" = Darwin ] || { echo "macOS only" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

MAP="$WORK/map"        #referenced name  -> @rpath/<base>
: > "$MAP"
COPIED="$WORK/copied"  #<base> -> source file
: > "$COPIED"
INODES="$WORK/inodes" #device:inode -> <base> already copied
: > "$INODES"

# Where an @rpath reference might live. Resolved once so the script still works
# if Homebrew is somewhere other than /opt/homebrew.
SEARCH_DIRS=(
    "$(brew --prefix llvm 2>/dev/null)/lib/c++"
    "$(brew --prefix llvm 2>/dev/null)/lib/unwind"
    "$(brew --prefix wxwidgets 2>/dev/null)/lib"
    "$(brew --prefix glib 2>/dev/null)/lib"
    "$(brew --prefix gettext 2>/dev/null)/lib"
    "$(brew --prefix pcre2 2>/dev/null)/lib"
    "$(brew --prefix 2>/dev/null)/lib"
)

map_get() { awk -F'\t' -v k="$1" '$1==k { print $2; exit }' "$MAP"; }
map_put() { printf '%s\t%s\n' "$1" "$2" >> "$MAP"; }
copied_get() { awk -F'\t' -v k="$1" '$1==k { print $2; exit }' "$COPIED"; }
ino_get() { awk -F'\t' -v k="$1" '$1==k { print $2; exit }' "$INODES"; }

is_system() {
    case "$1" in
        /usr/lib/*|/System/*) return 0 ;;
        *) return 1 ;;
    esac
}

# Resolve one dependency to a file on disk: absolute install names as-is, and
# @rpath names by searching next to the referrer first (libc++ uses those for
# libc++abi/libunwind), then the Homebrew prefixes above.
resolve() {
    local dep="$1" referrer="$2" base d
    case "$dep" in
        @rpath/*)
            base="${dep#@rpath/}"
            for d in "$(dirname "$referrer")" "${SEARCH_DIRS[@]}"; do
                [ -n "$d" ] && [ -e "$d/$base" ] && { echo "$d/$base"; return 0; }
            done
            return 1 ;;
        /*) [ -e "$dep" ] && { echo "$dep"; return 0; }; return 1 ;;
        *) return 1 ;;
    esac
}

deps_of() { otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}'; }

mkdir -p "$FW"

# Breadth-first from the executable through everything copied.
QUEUE=("$BIN")
while [ "${#QUEUE[@]}" -gt 0 ]; do
    current="${QUEUE[0]}"
    QUEUE=("${QUEUE[@]:1}")

    for dep in $(deps_of "$current"); do
        [ -n "$dep" ] || continue
        case "$dep" in */Offload) continue ;; esac   #its own install name
        is_system "$dep" && continue

        base="$(basename "$dep")"
        if [ -n "$(copied_get "$base")" ]; then
            map_put "$dep" "@rpath/$base"
            continue
        fi

        src="$(resolve "$dep" "$current")" || { echo "cannot resolve $dep (from $current)" >&2; exit 1; }

        # wx publishes libfoo-3.3.dylib -> libfoo-3.3.4.dylib -> libfoo-3.3.4.0.0.dylib,
        # and different images reference different links in that chain. Copying each
        # name would put the same code in the bundle twice under two install names,
        # so dedupe on the file they all point at. -L is required: macOS stat uses
        # lstat by default, which would give each symlink in the chain its own id.
        ino="$(stat -L -f '%d:%i' "$src")"
        already="$(ino_get "$ino")"
        if [ -n "$already" ]; then
            map_put "$dep" "@rpath/$already"
            map_put "$src" "@rpath/$already"
            [ "$dep" != "$src" ] && echo "  = $base -> $already (same file)"
            continue
        fi

        # Running this twice must not fail on "are identical (not copied)": the
        # second pass sees the already-bundled file through the @rpath references
        # it wrote in the first pass. -ef compares the underlying files.
        if [ -e "$FW/$base" ] && [ "$src" -ef "$FW/$base" ]; then
            map_put "$dep" "@rpath/$base"
            continue
        fi

        # -L follows the versioned symlink; we keep the name the load command
        # uses, because that is what dyld will look for.
        cp -L "$src" "$FW/$base"
        printf '%s\t%s\n' "$base" "$src" >> "$COPIED"
        printf '%s\t%s\n' "$ino" "$base" >> "$INODES"
        map_put "$dep" "@rpath/$base"
        [ "$dep" != "$src" ] && map_put "$src" "@rpath/$base"
        QUEUE+=("$FW/$base")
        echo "  + $base"
    done
done

echo "rewriting load commands"
for f in "$FW"/*.dylib "$BIN"; do
    chmod u+w "$f" 2>/dev/null || true
    codesign --remove-signature "$f" 2>/dev/null || true
    for dep in $(deps_of "$f"); do
        new="$(map_get "$dep")"
        [ -n "$new" ] && install_name_tool -change "$dep" "$new" "$f"
    done
done

# Each dylib's own identity, plus an rpath so @rpath siblings resolve from the
# Frameworks directory. @executable_path/../Frameworks covers the executable.
for f in "$FW"/*.dylib; do
    base="$(basename "$f")"
    install_name_tool -id "@rpath/$base" "$f"
    install_name_tool -add_rpath "@loader_path" "$f" 2>/dev/null || true
done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN" 2>/dev/null || true

echo "signing"
for f in "$FW"/*.dylib; do codesign --force --sign - "$f" >/dev/null; done
codesign --force --sign - "$APP"

# Fail loudly if anything still points outside the bundle.
echo "checking"
if otool -L "$BIN" "$FW"/*.dylib | grep -E "^[[:space:]]+/(opt|usr/local)/"; then
    echo "FAIL: absolute third-party paths remain" >&2
    exit 1
fi
echo "ok: $(ls "$FW" | wc -l | tr -d ' ') dylibs bundled, no absolute third-party paths"
