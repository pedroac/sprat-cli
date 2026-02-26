#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: profile_resolution_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"

case "$(uname)" in
    MINGW*|MSYS*) ;;
    *)
        echo "Skipping USERPROFILE resolution test on non-Windows shell."
        exit 0
        ;;
esac

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

tmp_dir_win="$(cygpath -m "$tmp_dir")"
fix_path() {
    echo "${1/$tmp_dir/$tmp_dir_win}"
}

frames_dir="$tmp_dir/frames"
mkdir -p "$frames_dir"

cat > "$tmp_dir/pixel.b64" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO7ZxaoAAAAASUVORK5CYII=
EOF
base64 -d "$tmp_dir/pixel.b64" > "$frames_dir/frame_a.png"
cp "$frames_dir/frame_a.png" "$frames_dir/frame_b.png"

frames_list_file="$tmp_dir/frames_list.txt"
cat > "$frames_list_file" <<EOF
$(fix_path "$frames_dir/frame_a.png")
$(fix_path "$frames_dir/frame_b.png")
EOF

userprofile_root="$tmp_dir/userprofile"
profiles_cfg="$userprofile_root/.config/sprat/spratprofiles.cfg"
mkdir -p "$(dirname "$profiles_cfg")"

cat > "$profiles_cfg" <<'EOF'
[profile fast]
mode=compact
optimize=space
trim_transparent=true
EOF

default_layout_file="$tmp_dir/layout_default.txt"
explicit_layout_file="$tmp_dir/layout_explicit.txt"

# Force Windows-native config discovery to use USERPROFILE and ignore HOME.
export HOME="/definitely-not-a-windows-path"
export USERPROFILE="$(cygpath -w "$userprofile_root")"

"$spratlayout_bin" "$(fix_path "$frames_list_file")" --padding 1 > "$default_layout_file"
"$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile fast --profiles-config "$(fix_path "$profiles_cfg")" --padding 1 > "$explicit_layout_file"

if ! cmp -s "$default_layout_file" "$explicit_layout_file"; then
    echo "Default run did not resolve config via USERPROFILE as expected" >&2
    exit 1
fi
