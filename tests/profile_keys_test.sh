#!/usr/bin/env bash
# Profiles saved by front ends such as sprat-gui use `preset`, `sort` and the keys of
# spratpack/spratconvert. spratlayout must read them, and still reject unknown keys.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: profile_keys_test.sh <spratlayout-bin>" >&2
    exit 1
fi

spratlayout_bin="$1"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$(uname)" == MINGW* || "$(uname)" == MSYS* ]]; then
    tmp_dir_win="$(cygpath -m "$tmp_dir")"
    fix_path() {
        echo "${1/$tmp_dir/$tmp_dir_win}"
    }
else
    fix_path() {
        echo "$1"
    }
fi

create_png() {
    local path="$1"
    local w="$2"
    local h="$3"
    if command -v magick >/dev/null; then
        magick -size "${w}x${h}" xc:red "$path"
    elif command -v convert >/dev/null; then
        convert -size "${w}x${h}" xc:red "$path"
    else
        echo "Error: ImageMagick not found" >&2
        exit 1
    fi
}

mkdir -p "$tmp_dir/frames"
create_png "$tmp_dir/frames/a.png" 10 10
create_png "$tmp_dir/frames/b.png" 40 40
create_png "$tmp_dir/frames/c.png" 20 20
create_png "$tmp_dir/frames/d.png" 30 30

# Listed out of name order, so `sort=name` is visible in the output.
frames_list="$tmp_dir/frames.txt"
for name in d c b a; do
    fix_path "$tmp_dir/frames/$name.png"
done > "$frames_list"

config="$tmp_dir/spratprofiles.cfg"
cat > "$config" <<'EOF'
[profile gui]
label=GUI profile
preset=small
padding=1
trim_transparent=false
sort=name
dilate=2
gpu_compress=dxt5
image_format=webp
image_quality=90
zopfli=true
frame_lines=true
frame_line_width=2
frame_line_color=0,255,0,255
atlas_index=0
auto_animations=true
EOF

run_layout() {
    "$spratlayout_bin" --stdin-list "$@" < "$frames_list"
}

sprite_names() {
    grep '^sprite ' | sed -E 's/^sprite "[^"]*[\\/]([^"\\/]*)".*$/\1/' | tr -d '\r' | tr '\n' ' '
}

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

# The profile lays out like the same options given as flags.
profile_layout="$(run_layout --profiles-config "$(fix_path "$config")" --profile gui)" \
    || fail "spratlayout rejected a profile with preset, sort and other tools' keys"
flags_layout="$(run_layout --preset small --padding 1 --sort name)"
[ "$profile_layout" = "$flags_layout" ] \
    || fail "profile layout differs from --preset small --padding 1 --sort name"

order="$(printf '%s\n' "$profile_layout" | sprite_names)"
[ "$order" = "a.png b.png c.png d.png " ] || fail "sort=name not applied, got order: $order"

# A --sort flag still wins over the profile's sort.
override_layout="$(run_layout --profiles-config "$(fix_path "$config")" --profile gui --sort none)"
[ "$override_layout" = "$(run_layout --preset small --padding 1 --sort none)" ] \
    || fail "--sort none did not override sort=name"
[ "$override_layout" != "$profile_layout" ] || fail "--sort none gave the sort=name layout"

# sprat-gui keeps the source resolution in the project, so its profiles carry only a target:
# it scales once a source is given, and is skipped (unscaled) without one.
target_config="$tmp_dir/target.cfg"
printf '[profile hd]\nmode=fast\ntarget_resolution=1280x720\n' > "$target_config"
scale_line="$(run_layout --profiles-config "$(fix_path "$target_config")" --profile hd | grep '^scale ' | tr -d '\r')" \
    || fail "a profile target_resolution without a source resolution was rejected"
[ "$scale_line" = "scale 1" ] || fail "target_resolution without a source scaled the layout: $scale_line"
scale_line="$(run_layout --profiles-config "$(fix_path "$target_config")" --profile hd \
    --source-resolution 2560x1440 | grep '^scale ' | tr -d '\r')"
[ "$scale_line" = "scale 0.5" ] || fail "target_resolution with --source-resolution gave: $scale_line"

# Typos and bad values are still errors.
expect_config_error() {
    local description="$1"
    local entry="$2"
    local expected="$3"
    local bad_config="$tmp_dir/bad.cfg"
    printf '[profile bad]\nmode=fast\n%s\n' "$entry" > "$bad_config"
    local stderr_file="$tmp_dir/stderr.txt"
    if run_layout --profiles-config "$(fix_path "$bad_config")" --profile bad >/dev/null 2>"$stderr_file"; then
        fail "$description was accepted"
    fi
    grep -q "$expected" "$stderr_file" || fail "$description: unexpected error: $(cat "$stderr_file")"
}

expect_config_error "an unknown key" "paddding=2" "unknown key 'paddding'"
expect_config_error "an invalid preset" "preset=tiny" "invalid preset 'tiny'"
expect_config_error "an invalid sort" "sort=size" "invalid sort 'size'"

echo "profile_keys_test passed"
