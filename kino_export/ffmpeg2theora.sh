#!/bin/sh

usage()
{
	# Title
	echo "Title: OggTheora Export(ffmpeg2theora)"

	# Usable?
	command -v ffmpeg2theora --help 2> /dev/null
	[ $? -eq 0 ] && echo Status: Active || echo Status: Inactive

	# Type
	echo Flags: single-pass file-producer
}

execute()
{
	# Arguments
	normalisation="$1"
	length="$2"
	profile="$3"
	file="$4"

	# Determine info arguments
	size=`[ "$normalisation" = "pal" ] && echo 352x288 || echo 352x240`
	video_bitrate=1152
	audio_bitrate=224

	# Run the command
	#ffmpeg -f dv -i - -f vcd -deinterlace -r "$normalisation" -s "$size" -b "$video_bitrate" -acodec mp2 -ab "$audio_bitrate" -y "$file".mpeg
	[ "x$file" = "x" ] && file="kino_export_"`date +%Y-%m-%d_%H.%M.%S`
	ffmpeg2theora -f dv -o "$file".ogg -
}

[ "$1" = "--usage" ] || [ -z "$1" ] && usage "$@" || execute "$@"
