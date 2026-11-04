#!/usr/bin/env bash
# Linux-side helper for run.bat. Each subcommand is one step of a launch.
cd "$(dirname "$0")"

case "${1:-}" in
build)
    # Rebuild if the programs are missing, a source file is newer, or they were
    # built against a different Ubuntu/CycloneDDS (those can't talk to each other).
    stamp="$(lsb_release -rs) cyclonedds-$(pkg-config --modversion CycloneDDS)"
    if [ -x server ] && [ -x client ] && [ "$(cat .build_stamp 2>/dev/null)" = "$stamp" ] &&
       ! find . -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' -o -name '*.idl' \) -newer server | grep -q .; then
        exit 0
    fi
    echo "Building for Ubuntu $stamp..."
    bash build.sh && echo "$stamp" > .build_stamp
    ;;
stop)
    # A game left over from an earlier run would answer logins too.
    pkill -x client; pkill -x server; pkill -f 'share_certs.py serve'; sleep 1; true
    ;;
host-certs)
    # The host is the source of the certificates. A laptop that last joined only
    # has the client's half, so it makes a fresh set before hosting.
    [ -f security/generated/server.key ] || { rm -rf security/generated; bash security/make_certs.sh; }
    ;;
server-host)
    # Share the certificates for as long as the server runs.
    python3 security/share_certs.py serve > certs.log 2>&1 &
    sharer=$!
    ./server > server.log 2>&1
    kill "$sharer" 2>/dev/null
    ;;
server-solo)
    ./server > server.log 2>&1
    ;;
fetch)
    python3 security/share_certs.py fetch
    ;;
*)
    echo "usage: launch.sh build|stop|host-certs|server-host|server-solo|fetch" >&2
    exit 2
    ;;
esac
