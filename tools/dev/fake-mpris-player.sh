#!/usr/bin/env bash
# Plays a file through mpv (no audio/video output) exposing it over MPRIS, to test media integration
# (song title, artist, timeline, album art) without touching your real player.
#
# usage: tools/dev/fake-mpris-player.sh <file> [seconds]
#   stop it with: pkill -x mpv
#
# Generate a test track with embedded cover art:
#   ffmpeg -f lavfi -i "sine=frequency=220:duration=200" -i cover.png -map 0:a -map 1:v \
#     -c:a libmp3lame -c:v mjpeg -id3v2_version 3 -disposition:v attached_pic \
#     -metadata title="A Very Long Song Title" -metadata artist="Artist" -metadata album="Album" test.mp3
set -eu

file="${1:?file required}"
plugin="${MPV_MPRIS_PLUGIN:?run inside the dev shell (nix develop) or set MPV_MPRIS_PLUGIN}"

nohup timeout "${2:-900}" mpv --no-config --ao=null --vo=null --loop-file=inf --script="$plugin" "$file" \
	>/tmp/fake-mpris-player.log 2>&1 &

echo "mpv started (pid $!), check with: busctl --user list | grep mpris"
