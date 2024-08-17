#!/usr/bin/env bash

test_init() {
	[[ -n ${MPD_T} ]] && return
	local dir
	[[ -n ${MPD_TEST_DIR} ]] \
		&& dir=${MPD_TEST_DIR} \
		|| dir=$(mktemp -qdp /var/tmp)
	mkdir -p "${dir}"
	echo "tests setup to take place in ${dir}"
	export MPD_T="${dir}"
	export MPD_TESTS="${BASH_SOURCE[0]%/*}"
	export OWNTONE_SRC="${BASH_SOURCE[0]%/*}/../src"
	# DYLD_LIBRARY_PATH? macOS path should be tested
	export LD_LIBRARY_PATH="${OWNTONE_SRC}/../sqlext/.libs"

	[[ -n ${MPD_TEST_DIR} ]] || trap 'test_cleanup' EXIT
}

test_cleanup() {
	server_shutdown
	[[ -n ${MPD_T} ]] && rm -Rf "${MPD_T}"
}

server_create_config() {
	test_init

  	# create minimal config to start an MPD server and serve some files
  	cat > "${MPD_T}"/owntone.conf <<- EOC
		general {
			uid = "$(whoami)"
			db_path = "${MPD_T}/songs3.db"
			logfile = "${MPD_T}/owntone.log"
			websocket_port = 13688
			cache_dir = "${MPD_T}/cache"
		}
		library {
			name = "MPD test instance"
			port = 13689
			directories = "${MPD_T}/music"
		}
		audio {
			nickname = "MPDoutput1"
			type = "dummy"
		}
		mpd {
			port = 16600
			enable_httpd_plugin = true
		}
	EOC

	mkdir -p "${MPD_T}"/music

	# use local cache to shortcut this
	if [[ -d ${MPD_TESTS}/music ]] ; then
		cp -a "${MPD_TESTS}"/music/* "${MPD_T}"/music/
		return
	fi

	# get some songs for our database, really this is a completely
	# random list, it is public domain, we could cache it in the repo
	for f in \
		"https://www.openmusicarchive.org/audio/Waiting_For_A_Train.mp3" \
		"https://www.openmusicarchive.org/audio/ATL/ATL%20BEAT%2010.mp3" \
		"https://www.openmusicarchive.org/audio/Georgia%20Stomp%20By%20Dj%20Assault.mp3" \
		"https://www.openmusicarchive.org/audio/For_Months_And_Months_And_Months.mp3" \
		"https://www.openmusicarchive.org/audio/One_Dime_Blues.mp3"
	do
		(cd "${MPD_T}"/music && wget "${f}")
	done
}

server_start() {
	test_init

	[[ -f "${MPD_T}"/owntone.conf ]] || server_create_config

	"${OWNTONE_SRC}"/owntone -f -d5 \
		-c "${MPD_T}"/owntone.conf \
		--mdns-no-rsp \
		--mdns-no-daap \
		--mdns-no-cname \
		--mdns-no-web \
		> ${MPD_T}/owntone.out 2>&1 &
	export OWNTONE_PID=$!

	while true ; do
		sleep 1
		nc -z localhost 16600 && break
	done
}

server_shutdown() {
	[[ -n ${OWNTONE_PID} ]] || return
	kill ${OWNTONE_PID}
	waitpid -t 4 ${OWNTONE_PID} || kill -KILL ${OWNTONE_PID}
}

run_test() {
	local tst=$1

	[[ -e ${MPD_TESTS}/${tst}.mpdtest ]] || return 1

	nc -q0 localhost 16600 < "${MPD_TESTS}"/${tst}.mpdtest \
		> "${MPD_T}"/${tst}.mpdout 2>&1

	if diff -q "${MPD_TESTS}"/${tst}.mpdout "${MPD_T}"/${tst}.mpdout ; then
		echo "[ok] ${tst}"
	else
		echo "[!!] ${tst}:"
		diff -Nu "${MPD_TESTS}"/${tst}.mpdout "${MPD_T}"/${tst}.mpdout
	fi
}

DEFAULT_TESTS=(
	#"outputs"  # until we can disable avahi to pick up random stuff
	"volume"
)

if [[ $# -gt 0 ]] ; then
	TESTS=( "${@}" )
else
	TESTS=( "${DEFAULT_TESTS[@]}" )
fi

echo "${TESTS[@]}"
for t in "${TESTS[@]}" ; do
	# each test gets a fresh server instance to wipe any state
	server_start

	run_test "${t}"

	server_shutdown
done
