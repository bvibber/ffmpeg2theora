#!/bin/sh
#
# place this script in/usr/share/kino/scripts/exports
# and make sure ffmpeg2theora is in $PATH
#
#
usage()
{
	# Title
	echo "Title: Ogg Theora Export"

	# Usable?
	command -v ffmpeg2theora  2>&1 > /dev/null
	[ $? -eq 0 ] && echo Status: Active || echo Status: Inactive

	# Type
	echo Flags: single-pass file-producer

	echo Profile: v2v Preview
	echo Profile: v2v Pro
}

execute()
{
	# Arguments
	normalisation="$1"
	length="$2"
	profile="$3"
	file="$4"
	case "$profile" in
		"0" )
			preset="preview";;
		"1" )
			preset="pro";;
	# Determine info arguments
	size=`[ "$normalisation" = "pal" ] && echo 352x288 || echo 352x240`
	video_bitrate=1152
	audio_bitrate=224

	# Run the command
	[ "x$file" = "x" ] && file="kino_export_"`date +%Y-%m-%d_%H.%M.%S`
	ffmpeg2theora -f dv -p $preset -o "$file".ogg -
}

[ "$1" = "--usage" ] || [ -z "$1" ] && usage "$@" || execute "$@"
