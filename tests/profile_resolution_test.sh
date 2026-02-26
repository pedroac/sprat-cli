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
        echo "Skipping Windows profile resolution test on non-Windows shell."
        exit 0
        ;;
esac

tmp_dir="$(mktemp -d)"
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

write_cfg() {
    local path="$1"
    local padding="$2"
    mkdir -p "$(dirname "$path")"
    cat > "$path" <<EOF
[profile probe]
mode=fast
padding=$padding
trim_transparent=false
EOF
}

normalize_eol() {
    tr -d '\r' < "$1"
}

files_match_text() {
    local left="$1"
    local right="$2"
    if diff -u <(normalize_eol "$left") <(normalize_eol "$right") >/dev/null; then
        return 0
    fi
    return 1
}

run_profile() {
    local out_file="$1"
    (
        cd "$tmp_dir"
        "$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile probe > "$out_file"
    )
}

assert_matches_explicit() {
    local expected_cfg="$1"
    local label="$2"
    local implicit_out="$tmp_dir/${label}_implicit.txt"
    local explicit_out="$tmp_dir/${label}_explicit.txt"

    run_profile "$implicit_out"
    (
        cd "$tmp_dir"
        "$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile probe --profiles-config "$(fix_path "$expected_cfg")" > "$explicit_out"
    )

    if ! files_match_text "$implicit_out" "$explicit_out"; then
        echo "Profile lookup did not match expected source: $label" >&2
        echo "--- implicit output ---" >&2
        cat "$implicit_out" >&2 || true
        echo "--- explicit output ($expected_cfg) ---" >&2
        cat "$explicit_out" >&2 || true
        echo "--- diff -u ---" >&2
        diff -u "$implicit_out" "$explicit_out" >&2 || true
        echo "--- profile debug rerun (stderr) ---" >&2
        (
            cd "$tmp_dir"
            SPRAT_PROFILE_DEBUG=1 "$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile probe || true
            SPRAT_PROFILE_DEBUG=1 "$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile probe --profiles-config "$(fix_path "$expected_cfg")" || true
        )
        exit 1
    fi
}

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

appdata_root="$tmp_dir/appdata"
userprofile_root="$tmp_dir/userprofile"
home_root="$tmp_dir/home"
mkdir -p "$appdata_root" "$userprofile_root" "$home_root"

appdata_cfg="$appdata_root/sprat/spratprofiles.cfg"
cwd_cfg="$tmp_dir/spratprofiles.cfg"

export APPDATA="$(cygpath -m "$appdata_root")"
export USERPROFILE="$(cygpath -m "$userprofile_root")"
export HOME="$(cygpath -m "$home_root")"

# 1) User config (APPDATA) should be preferred when present.
write_cfg "$appdata_cfg" 3
write_cfg "$cwd_cfg" 7
assert_matches_explicit "$appdata_cfg" "appdata"

# 2) Current directory config should be used when user config is absent.
rm -f "$appdata_cfg"
write_cfg "$cwd_cfg" 7
assert_matches_explicit "$cwd_cfg" "cwd"

# 3) Precedence: APPDATA > current directory.
write_cfg "$cwd_cfg" 9
write_cfg "$appdata_cfg" 5
assert_matches_explicit "$appdata_cfg" "precedence"
