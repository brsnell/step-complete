#!/usr/bin/env bash
# Regenerate the STEP Complete application icons from the master SVG.
#
# Source:  resources/icons/stepcomplete.svg
# Outputs:
#   resources/icons/png/stepcomplete-{16,24,32,48,64,128,256,512}.png
#   packaging/windows/stepcomplete.ico      (16,24,32,48,64,256)
#   packaging/macos/stepcomplete.icns       (via make_icns.py)
#   packaging/linux/stepcomplete.png        (256)
#
# Renderer: rsvg-convert if present, else ImageMagick (magick or convert).
# Optional: optipng, to shrink the PNGs when installed.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/../.." && pwd)"

svg="$root/resources/icons/stepcomplete.svg"
png_dir="$root/resources/icons/png"
ico_out="$root/packaging/windows/stepcomplete.ico"
icns_out="$root/packaging/macos/stepcomplete.icns"
linux_out="$root/packaging/linux/stepcomplete.png"

png_sizes=(16 24 32 48 64 128 256 512)
ico_sizes=(16 24 32 48 64 256)

if command -v rsvg-convert >/dev/null 2>&1; then
    renderer="rsvg"
elif command -v magick >/dev/null 2>&1; then
    renderer="magick"
elif command -v convert >/dev/null 2>&1; then
    renderer="convert"
else
    echo "error: need rsvg-convert, magick or convert to render the SVG" >&2
    exit 1
fi

# render_png SIZE OUT: rasterise the master SVG at SIZE x SIZE pixels.
render_png() {
    local size="$1" out="$2"
    case "$renderer" in
        rsvg)
            rsvg-convert -w "$size" -h "$size" -f png -o "$out" "$svg"
            ;;
        magick)
            magick -background none -density 1024 "$svg" -resize "${size}x${size}" "PNG32:$out"
            ;;
        convert)
            convert -background none -density 1024 "$svg" -resize "${size}x${size}" "PNG32:$out"
            ;;
    esac
}

# The 1024 px rendition is only needed for the icns 512@2x slot.
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/stepcomplete-icons.XXXXXX")"
trap 'rm -rf "$tmp_dir"' EXIT

mkdir -p "$png_dir" "$(dirname "$ico_out")" "$(dirname "$icns_out")" "$(dirname "$linux_out")"

echo "Renderer: $renderer"

for size in "${png_sizes[@]}"; do
    render_png "$size" "$png_dir/stepcomplete-$size.png"
done
render_png 1024 "$tmp_dir/stepcomplete-1024.png"

# Windows .ico from the sizes listed above.
ico_inputs=()
for size in "${ico_sizes[@]}"; do
    ico_inputs+=("$png_dir/stepcomplete-$size.png")
done
convert "${ico_inputs[@]}" "$ico_out"

# macOS .icns: standard types, with @2x variants.
python3 "$script_dir/make_icns.py" "$icns_out" \
    ic07="$png_dir/stepcomplete-128.png" \
    ic08="$png_dir/stepcomplete-256.png" \
    ic09="$png_dir/stepcomplete-512.png" \
    ic10="$tmp_dir/stepcomplete-1024.png" \
    ic11="$png_dir/stepcomplete-32.png" \
    ic12="$png_dir/stepcomplete-64.png" \
    ic13="$png_dir/stepcomplete-256.png" \
    ic14="$png_dir/stepcomplete-512.png"

# Linux desktop icon.
cp "$png_dir/stepcomplete-256.png" "$linux_out"

if command -v optipng >/dev/null 2>&1; then
    optipng -quiet -o2 "$png_dir"/stepcomplete-*.png "$linux_out"
fi

echo "Wrote icons under $root/resources/icons, $root/packaging/{windows,macos,linux}"
