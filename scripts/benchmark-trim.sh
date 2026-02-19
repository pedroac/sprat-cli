#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
  echo "Usage: benchmark-trim.sh <spratlayout-bin> [source-dir] [runs]" >&2
  exit 1
fi

spratlayout_bin="$1"
source_dir="${2:-./frames}"
runs="${3:-5}"
source_dir_abs="$(cd "$source_dir" && pwd)"

if [ ! -x "$spratlayout_bin" ]; then
  echo "Error: spratlayout binary is not executable: $spratlayout_bin" >&2
  exit 1
fi

if [ ! -d "$source_dir" ]; then
  echo "Error: source dir not found: $source_dir" >&2
  exit 1
fi

if ! [[ "$runs" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: runs must be a positive integer" >&2
  exit 1
fi

now_ns() {
  local ns
  ns="$(date +%s%N)"
  if [[ "$ns" == *N ]]; then
    echo $(( $(date +%s) * 1000000000 ))
  else
    echo "$ns"
  fi
}

collect_sources() {
  find "$source_dir_abs" -type f \
    \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.bmp' -o -iname '*.tga' \) \
    | sort
}

run_case() {
  local case_name="$1"
  local repeat_count="$2"

  local case_dir="$tmp_dir/$case_name"
  mkdir -p "$case_dir"

  local manifest_template="$case_dir/manifest.template.txt"
  : > "$manifest_template"

  local count=0
  while IFS= read -r path; do
    if [ -z "$path" ]; then
      continue
    fi
    count=$((count + 1))
    local i
    for ((i = 0; i < repeat_count; ++i)); do
      printf '%s\n' "$path" >> "$manifest_template"
    done
  done < <(collect_sources)

  if [ "$count" -eq 0 ]; then
    echo "Error: no supported images found in $source_dir" >&2
    exit 1
  fi

  local total_no_trim=0
  local total_trim=0
  local run
  for ((run = 1; run <= runs; ++run)); do
    local manifest="$case_dir/manifest.run.${run}.txt"
    cp "$manifest_template" "$manifest"

    local t0 t1 d_no_trim d_trim

    t0="$(now_ns)"
    "$spratlayout_bin" "$manifest" --profile fast > /dev/null
    t1="$(now_ns)"
    d_no_trim=$(( (t1 - t0) / 1000000 ))

    t0="$(now_ns)"
    "$spratlayout_bin" "$manifest" --profile fast --trim-transparent > /dev/null
    t1="$(now_ns)"
    d_trim=$(( (t1 - t0) / 1000000 ))

    total_no_trim=$((total_no_trim + d_no_trim))
    total_trim=$((total_trim + d_trim))
  done

  local avg_no_trim=$((total_no_trim / runs))
  local avg_trim=$((total_trim / runs))
  local delta=$((avg_trim - avg_no_trim))

  local ratio
  ratio="$(awk -v a="$avg_trim" -v b="$avg_no_trim" 'BEGIN { if (b == 0) { print "n/a" } else { printf "%.2fx", a / b } }')"

  local entries=$((count * repeat_count))
  printf '%-8s | %7d | %5d | %8d | %7d | %s\n' "$case_name" "$count" "$entries" "$avg_no_trim" "$avg_trim" "$ratio"
  printf '          delta(trim-no_trim): %d ms\n' "$delta"
}

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

printf 'Trim benchmark\n'
printf 'spratlayout: %s\n' "$spratlayout_bin"
printf 'source_dir : %s\n' "$source_dir_abs"
printf 'runs/case  : %s\n\n' "$runs"
printf '%-8s | %7s | %5s | %8s | %7s | %s\n' "case" "sources" "items" "no_trim" "trim" "ratio"
printf '%s\n' '---------+---------+-------+----------+---------+-------'

run_case "small" 1
run_case "medium" 10
run_case "large" 40
