#!/usr/bin/env bash
# Linux-side helper for run.bat. Each subcommand is one step of a launch.
cd "$(dirname "$0")"
# Fixed DDS ports (7400-7449 on domain 0) instead of random ones, so
# firewall.ps1 can open just these while the game runs.
export CYCLONEDDS_URI='<Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>19</MaxAutoParticipantIndex></Discovery>'

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
    # A game left over from an earlier run would answer logins too, and a
    # running program can't be overwritten by the build.
    pkill -x client; pkill -x server; sleep 1; true
    ;;
certs)
    # The certificates are committed, so this only runs if they went missing.
    [ -f security/generated/permissions.p7s ] || bash security/make_certs.sh
    ;;
server)
    exec ./server > server.log 2>&1
    ;;
client)
    exec ./client "$2" > "client$2.log" 2>&1
    ;;
wait)
    # Tell firewall.ps1 when every game program has closed, so it can remove
    # its firewall rule. $2 is that run's token.
    sleep 15
    while pgrep -x server >/dev/null || pgrep -x client >/dev/null; do sleep 3; done
    touch ".game_closed_$2"
    ;;
*)
    echo "usage: launch.sh build|stop|certs|server|client N|wait TOKEN" >&2
    exit 2
    ;;
esac
