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

run_profile() {
    local out_file="$1"
    "$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile probe > "$out_file"
}

assert_matches_explicit() {
    local expected_cfg="$1"
    local label="$2"
    local implicit_out="$tmp_dir/${label}_implicit.txt"
    local explicit_out="$tmp_dir/${label}_explicit.txt"

    run_profile "$implicit_out"
    "$spratlayout_bin" "$(fix_path "$frames_list_file")" --profile probe --profiles-config "$(fix_path "$expected_cfg")" > "$explicit_out"

    if ! cmp -s "$implicit_out" "$explicit_out"; then
        echo "Profile lookup did not match expected source: $label" >&2
        exit 1
    fi
}

exe_dir="$(dirname "$spratlayout_bin")"
exe_cfg="$exe_dir/spratprofiles.cfg"
exe_cfg_backup="$tmp_dir/exe_spratprofiles.backup"
exe_cfg_had_backup=0

if [ -f "$exe_cfg" ]; then
    cp "$exe_cfg" "$exe_cfg_backup"
    exe_cfg_had_backup=1
fi

cleanup() {
    if [ "$exe_cfg_had_backup" -eq 1 ]; then
        cp "$exe_cfg_backup" "$exe_cfg"
    else
        rm -f "$exe_cfg"
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

appdata_root="$tmp_dir/appdata"
programdata_root="$tmp_dir/programdata"
userprofile_root="$tmp_dir/userprofile"
home_root="$tmp_dir/home"
mkdir -p "$appdata_root" "$programdata_root" "$userprofile_root" "$home_root"

appdata_cfg="$appdata_root/sprat/spratprofiles.cfg"
programdata_cfg="$programdata_root/Sprat/spratprofiles.cfg"

export APPDATA="$(cygpath -w "$appdata_root")"
export PROGRAMDATA="$(cygpath -w "$programdata_root")"
export USERPROFILE="$(cygpath -w "$userprofile_root")"
export HOME="$(cygpath -w "$home_root")"

# 1) APPDATA should be preferred when present.
write_cfg "$appdata_cfg" 3
rm -f "$exe_cfg" "$programdata_cfg"
assert_matches_explicit "$appdata_cfg" "appdata"

# 2) Exe directory should be used when APPDATA config is absent.
rm -f "$appdata_cfg" "$programdata_cfg"
write_cfg "$exe_cfg" 7
assert_matches_explicit "$exe_cfg" "exedir"

# 3) PROGRAMDATA should be used when APPDATA and exe-dir configs are absent.
rm -f "$appdata_cfg" "$exe_cfg"
write_cfg "$programdata_cfg" 11
assert_matches_explicit "$programdata_cfg" "programdata"

# 4) Precedence: APPDATA > exe-dir > PROGRAMDATA.
write_cfg "$programdata_cfg" 13
write_cfg "$exe_cfg" 9
write_cfg "$appdata_cfg" 5
assert_matches_explicit "$appdata_cfg" "precedence"
