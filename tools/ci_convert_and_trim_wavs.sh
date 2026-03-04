#!/usr/bin/env bash
set -euo pipefail

# CI-friendly script to: backup WAVs, trim silence, convert to mono PCM16@22050Hz,
# and print a per-file and total size savings report.
# Usage: ./ci_convert_and_trim_wavs.sh [source_dir]
# Defaults: source_dir=data

SRC_DIR=${1:-data}
BACKUP_DIR="$SRC_DIR/wav_backup_ci"
OUT_DIR="$SRC_DIR/mono_ci"
REPORT="$OUT_DIR/convert_report.csv"

mkdir -p "$BACKUP_DIR"
mkdir -p "$OUT_DIR"

command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg not found. Install ffmpeg in CI runner."; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "python3 not found. Install python3 in CI runner."; exit 2; }

# Function: compute total size (bytes) for files in a directory using python3 for portability
total_size() {
  python3 - <<PY
import sys,os
p=sys.argv[1]
size=0
for root,_,files in os.walk(p):
  for f in files:
    try:
      size+=os.path.getsize(os.path.join(root,f))
    except OSError:
      pass
print(size)
PY
 "$1"
}

echo "Converting WAV files in $SRC_DIR -> $OUT_DIR (backups in $BACKUP_DIR)"

# CSV header
printf "file,orig_bytes,converted_bytes,saved_bytes,saved_percent\n" > "$REPORT"

shopt -s nullglob
for f in "$SRC_DIR"/*.wav; do
  base=$(basename "$f")
  out="$OUT_DIR/$base"

  echo "Processing: $base"
  # Backup original if not already backed up
  if [ ! -f "$BACKUP_DIR/$base" ]; then
    cp -p "$f" "$BACKUP_DIR/$base"
  fi

  # Convert: trim silence at both ends then convert to mono PCM16 22050 Hz
  # The areverse trick trims end silence as well.
  ffmpeg -hide_banner -loglevel warning -y -i "$f" \
    -af "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.3,areverse,silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.3,areverse" \
    -ac 1 -ar 22050 -sample_fmt s16 -acodec pcm_s16le "$out"

  # Sizes
  orig=$(python3 - <<PY
import os,sys
print(os.path.getsize(sys.argv[1]))
PY
  "$f")
  conv=$(python3 - <<PY
import os,sys
print(os.path.getsize(sys.argv[1]))
PY
  "$out")
  saved=$((orig - conv))
  savedpct=0
  if [ "$orig" -gt 0 ]; then
    savedpct=$(awk "BEGIN{printf \"%.1f\", ($saved / $orig) * 100}")
  fi
  printf "%s,%s,%s,%s,%s\n" "$base" "$orig" "$conv" "$saved" "$savedpct" >> "$REPORT"
  echo "  orig=$(numfmt --to=iec --suffix=B $orig 2>/dev/null || echo ${orig}B) -> conv=$(numfmt --to=iec --suffix=B $conv 2>/dev/null || echo ${conv}B) saved=${savedpct}%"

done
shopt -u nullglob

# Totals
orig_total=$(total_size "$SRC_DIR")
conv_total=$(total_size "$OUT_DIR")
saved_total=$((orig_total - conv_total))
if [ "$orig_total" -gt 0 ]; then
  saved_total_pct=$(awk "BEGIN{printf \"%.1f\", ($saved_total / $orig_total) * 100}")
else
  saved_total_pct=0
fi

echo "\nConversion complete." 
echo "Original total: $orig_total bytes"
echo "Converted total: $conv_total bytes"
echo "Total saved: $saved_total bytes (${saved_total_pct}%)"

echo "Full CSV report: $REPORT"

# Exit with success
exit 0
