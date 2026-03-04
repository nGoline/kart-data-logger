#!/usr/bin/env bash
set -euo pipefail

# Batch-convert all WAVs in data/ to mono PCM16 @ 22050 Hz
# Backups originals to data/wav_backup/ and writes converted files to data/mono/
# Requires ffmpeg installed.

mkdir -p data/wav_backup
mkdir -p data/mono

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg not found. Install ffmpeg and re-run."
  exit 2
fi

for f in data/*.wav; do
  [ -f "$f" ] || continue
  base=$(basename "$f")
  echo "Converting $base ..."
  cp -n "$f" "data/wav_backup/$base"
  ffmpeg -hide_banner -loglevel warning -y -i "$f" -ac 1 -ar 22050 -sample_fmt s16 -acodec pcm_s16le "data/mono/$base"
done

echo "Converted files written to data/mono/. Originals backed up to data/wav_backup/."

echo "Quick checks:"
echo "  file data/mono/<name>.wav  # inspect format"
echo "  ffplay data/mono/<name>.wav  # play (if ffplay available)"

echo "If the converted files sound OK, replace originals with the mono versions before building to reduce LittleFS usage."
