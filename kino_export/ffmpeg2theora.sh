#!/bin/sh
# written by jan gerber <j@v2v.cc>
# profiles added by Dan Dennedy

usage()
{
	# Title
	echo "Title: Ogg Theora Export (ffmpeg2theora)"

	# Usable?
	which ffmpeg2theora > /dev/null
	[ $? -eq 0 ] && echo Status: Active || echo Status: Inactive

	# Type
	echo Flags: single-pass file-producer
	
	# Profiles
	echo "Profile: High Quality (640x480)"
	echo "Profile: Medium Quality (320x240)"
	echo "Profile: Broadband Quality (320x240)"
	echo "Profile: Low Quality (160x128)"
}

execute()
{
	# Arguments
	normalisation="$1"
	length="$2"
	profile="$3"
	file="$4"
	[ "x$file" = "x" ] && file="kino_export_"`date +%Y-%m-%d_%H.%M.%S`

	# Determine info arguments
	size=`[ "$normalisation" = "pal" ] && echo 352x288 || echo 352x240`
	video_bitrate=1152
	audio_bitrate=224

	# Run the command
	case "$profile" in 
		"0" ) 	ffmpeg2theora -f dv -x 640 -y 480 -d -v 7 -a 3 -H 48000 -o "$file".ogg - ;;
		"1" ) 	ffmpeg2theora -f dv -x 320 -y 240 -d -v 7 -a 3 -H 48000 -o "$file".ogg - ;;
		"2" ) 	ffmpeg2theora -f dv -x 320 -y 240 -d -v 5 -a 0 -H 44100 -o "$file".ogg - ;;
		"3" ) 	ffmpeg2theora -f dv -x 160 -y 128 -d -v 3 -a 0 -H 22000 -o "$file".ogg - ;;
	esac
}

[ "$1" = "--usage" ] || [ -z "$1" ] && usage "$@" || execute "$@"
