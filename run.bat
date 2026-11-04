@echo off
rem The Gingerbread Curse: double-click to play. No password needed.
rem Installs the dependencies and rebuilds only when needed, then opens the
rem server and both player windows.
rem Packages = Week 1 slide 8, plus pkg-config (build.sh needs it), openssl and
rem libssl-dev (DDS encryption certificates, password hashing) and libmongoc-dev
rem (MongoDB accounts, used only when a .env file is present).
setlocal
cd /d "%~dp0"

where wsl >nul 2>&1
if errorlevel 1 goto no_wsl

wsl --exec bash -c "command -v idlc && pkg-config --exists glfw3 CycloneDDS gl libcrypto libmongoc-1.0" >nul 2>&1
if not errorlevel 1 goto dependencies_ready
echo Installing dependencies, first run only. This takes a few minutes...
rem Running as WSL's root user is what avoids the sudo password prompt.
wsl -u root --exec bash -c "export DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a; apt-get update && apt-get install -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold libgl1-mesa-dev libglfw3-dev cyclonedds-dev cyclonedds-tools pkg-config openssl libssl-dev libmongoc-dev"
if errorlevel 1 goto failed
:dependencies_ready

rem sed undoes CRLF line endings, which stop .sh files running on Linux.
rem Rebuild only if the programs are missing or a source file is newer.
wsl --cd "%~dp0." --exec bash -c "sed -i 's/\r$//' build.sh debug_build.sh security/make_certs.sh; [ -x server ] && [ -x client ] && ! find . -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' -o -name '*.idl' \) -newer server | grep -q . || bash build.sh"
if errorlevel 1 goto failed

rem A game left over from an earlier run would answer logins too, so close old copies.
wsl --exec bash -c "pkill -x client; pkill -x server; sleep 1; true"

rem Terminal windows start minimized, so they can't cover the game or take its clicks.
rem Output goes to server.log, client1.log and client2.log.
start "Gingerbread Server" /min wsl --cd "%~dp0." --exec bash -c "./server > server.log 2>&1"
ping -n 3 127.0.0.1 >nul
start "Gingerbread Player 1" /min wsl --cd "%~dp0." --exec bash -c "./client 1 > client1.log 2>&1"
exit /b 0

:no_wsl
echo WSL is not installed. Run "wsl --install" in an admin PowerShell, restart, then try again.
pause
exit /b 1

:failed
echo.
echo Something failed - see the error above.
pause
exit /b 1
