#!/usr/bin/env bash

set -e

function read_source_files() {
	if [ -z "$RECORDING" ]; then
		echo "No recording specified"
		exit 1
	fi

	LISTING="./${RECORDING}.list"
	OUTPUT="./${RECORDING}.mkv"

	echo "file './GOPR${RECORDING}.MP4'" | tee "$LISTING" > /dev/null

	for FILE in ./GP*${RECORDING}.MP4; do
		echo "file '$FILE'" | tee -a "$LISTING" > /dev/null
	done

	echo "Found the following segments to join:"
	cat "$LISTING"
}

function read_global_state() {
	METADATA_GLOBAL="${RECORDING}.metadata.global"
	if [ -f "$METADATA_GLOBAL" ]; then
		echo "Using saved global state"
		source "$METADATA_GLOBAL"
	fi

	if [ -z "$OUR_TEAM" ]; then
		echo "Choose our team name (Dodgy Asians|Dodgier Asians):"
		read OUR_TEAM
	fi
	if [ -z "$THEIR_TEAM" ]; then
		echo "Choose their team name:"
		read THEIR_TEAM
	fi
	if [ -z "$MATCH_TYPE" ]; then
		echo "Choose match type (League|Trial|Friendly|Semifinal|Final):"
		read MATCH_TYPE
	fi
	if [ -z "$MATCH_NUMBER" ]; then
		echo "Choose match number (1,2,3...):"
		read MATCH_NUMBER
	fi

	if [ -z "$HALF" ]; then
		HALF=0
	fi
	if [ -z "$SET" ]; then
		SET=0
	fi
	if [ -z "$FOR" ]; then
		FOR=0
	fi
	if [ -z "$AGAINST" ]; then
		AGAINST=0
	fi
}

function save_global_state() {
	cat << EOF > "$METADATA_GLOBAL"
		OUR_TEAM="$OUR_TEAM"
		THEIR_TEAM="$THEIR_TEAM"
		MATCH_TYPE="$MATCH_TYPE"
		MATCH_NUMBER="$MATCH_NUMBER"

		END_TIME="$END_TIME"
		HALF="$HALF"
		SET="$SET"
		FOR="$FOR"
		AGAINST="$AGAINST"
EOF
}

function save_set_state() {
	cat << EOF > "${METADATA_SET}${SET}"
		START_TIME="$START_TIME"
		WON="$WON"
		HALF="$HALF"
		SET="$SET"
EOF
}

function load_set_state() {
	source "${METADATA_SET}${SET}"
}


function echo_state() {
	cat << EOF

	Score: $FOR-$AGAINST
	Half: $(( $HALF + 1 ))
	Sets so far: $(( $SET ))
EOF
}

function echo_root_prompt() {
	cat << EOF

	S: Start of a set
	$(if (( $HALF == 0 )); then echo "H: end of the first half"; else echo "E: End of the match"; fi)
EOF
}

function echo_set_prompt() {
	cat << EOF
	W: Set won
	L: Set lost
EOF
}

function read_time() {
	while true; do
		read TIMECODE
		if echo "$TIMECODE" > egrep '[0-9]{2}:[0-9]{2}:[0-9]{2}'; then
			HOURS=${TIMECODE:0:2}
			MINUTES=${TIMECODE:3:2}
			SECONDS=${TIMECODE:6:2}
			break
		else
			echo "Could not parse timestamp"
		fi
	done
	# START_TIME=$(( $HOURS * 60 * 60 + $MINUTES * 60 + $SECONDS ))
	TIME=$TIMECODE
}

function read_timeline() {
	while [ -z "$END_TIME" ]; do
		echo_state
		echo_root_prompt

		read ACTION

		case ${ACTION^^} in
		"S")
			echo "What timecode did the set start? (HH:MM:SS)"
			read_time
			START_TIME=$TIME
			echo "Set $(( $SET + 1 )) starting at $START_TIME"

			echo_set_prompt
			read ACTION
			case ${ACTION^^} in
			"W")
				echo "Won the point"
				FOR=$(( $FOR + 1 ))
				WON=true
				;;
			"L")
				echo "Lost the point"
				AGAINST=$(( $AGAINST + 1 ))
				WON=false
				;;
			esac
			save_set_state
			SET=$(( $SET + 1 ))
			save_global_state
			;;
		"H")
			echo "End of half $(( $HALF + 1 ))"
			HALF=$(( $HALF + 1 ))
			save_global_state
			;;
		"E")
			echo "What timecode did the match end? (HH:MM:SS)"
			read_time
			END_TIME=$TIME
			save_global_state
			;;
		*)
			echo "Unrecognised command \"$ACTION\""
		esac
	done
	echo "Information collection complete"
}

function tag() {
	read_global_state
	METADATA_SET="${RECORDING}.metadata.set"
	read_timeline
}

function join() {
	read_source_files
	ffmpeg -f concat -safe 0 -i "$LISTING" -c copy "$OUTPUT"
}

function split() {
	read_global_state
	echo_state
	NUM_SETS=$SET
	for CURRENT_SET in $(seq 0 $(( $NUM_SETS - 1 ))); do
		source "${RECORDING}.metadata.set$CURRENT_SET"
		SET_START=$START_TIME
		CURRENT_HALF=$HALF
		if (( $CURRENT_SET == $NUM_SETS - 1)); then
			source "${RECORDING}.metadata.global"
			SET_END=$END_TIME
		else
			source "${RECORDING}.metadata.set$(( CURRENT_SET + 1 ))"
			SET_END=$START_TIME
		fi
		FILENAME="$MATCH_TYPE $MATCH_NUMBER: $OUR_TEAM vs $THEIR_TEAM - H${HALF}S${SET}.mkv"
		ffmpeg -ss "$SET_START" -to "$SET_END" -i "${RECORDING}.mkv" -c copy "$FILENAME"
	done;
	return
}

function reset() {
	rm -rf "${RECORDING}.list" "${RECORDING}.metadata.global" "${RECORDING}.metadata.set"*
}

function help() {
	cat << EOF
Usage:
	$0 (-h|--help)
		Help
	$0 join <code>
		Join the segments of the video <code> together
	$0 tag <code>
		Interactively specify markers in the joined video
	$0 split <code>
		Split the match into a separate video for each set
	$0 reset <code>
		Reset data about sets, times, etc

EOF
}

case $1 in
"help")
	help
	;;
"-h")
	help
	;;
"--help")
	help
	;;
"")
	help
	;;
"join")
	RECORDING=$2
	join
	;;
"tag")
	RECORDING=$2
	tag
	;;
"split")
	RECORDING=$2
	split
	;;
"reset")
	RECORDING=$2
	reset
	;;
esac



# ffmpeg -f concat -safe 0 -i "$LISTING" -c copy "$OUTPUT"
