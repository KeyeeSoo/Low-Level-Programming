@echo off
rem The Gingerbread Curse: double-click to play.
rem   run.bat        asks which role this laptop plays
rem   run.bat host   this laptop runs the server and Player 1
rem   run.bat join   this laptop is Player 2 and connects to the host
rem   run.bat solo   server and both players on this laptop (testing)
rem Only ONE laptop may host: two servers on one network fight over the game
rem and players and ingredients flicker between them.
rem Everything runs in WSL's Ubuntu 24.04, installed here if it is missing, so
rem every laptop has the same CycloneDDS (different versions can't connect).
rem The DDS Security certificates are committed in security/generated/, so every
rem laptop already has the same ones.
setlocal
cd /d "%~dp0"
set "DISTRO=Ubuntu-24.04"
set "WSLD=wsl -d %DISTRO%"

where wsl >nul 2>&1
if errorlevel 1 goto no_wsl

rem ---- Which role? ----
set "ROLE=%~1"
if /i "%ROLE%"=="host" goto role_set
if /i "%ROLE%"=="join" goto role_set
if /i "%ROLE%"=="solo" goto role_set
echo.
echo   The Gingerbread Curse
echo   [1] Host - run the server and play as Player 1
echo   [2] Join - play as Player 2 on the host's game
echo   [3] Solo - server and both players on this laptop
echo.
choice /c 123 /n /m "Pick 1, 2 or 3: "
if errorlevel 3 (set "ROLE=solo") else if errorlevel 2 (set "ROLE=join") else (set "ROLE=host")
:role_set
echo Starting as: %ROLE%

rem ---- Ubuntu 24.04 ----
%WSLD% -u root --exec true >nul 2>&1
if not errorlevel 1 goto distro_ready
echo Installing %DISTRO% in WSL, first run only. This takes a few minutes...
wsl --install -d %DISTRO% --no-launch
%WSLD% -u root --exec true >nul 2>&1
if not errorlevel 1 goto distro_ready
rem Older WSL only registers the distro on first launch: do that as root, no prompts.
where ubuntu2404 >nul 2>&1
if not errorlevel 1 ubuntu2404 install --root
%WSLD% -u root --exec true >nul 2>&1
if errorlevel 1 goto distro_manual
:distro_ready

rem ---- Dependencies ----
rem Packages = Week 1 slide 8, plus build-essential (gcc, which a fresh Ubuntu
rem lacks), pkg-config (build.sh needs it), openssl and libssl-dev (DDS
rem encryption certificates, password hashing) and libmongoc-dev (MongoDB
rem accounts, used only when a .env file is present), libsdl2-dev and
rem libsdl2-mixer-dev (music and sound effects).
%WSLD% --exec bash -c "command -v gcc && command -v idlc && pkg-config --exists glfw3 CycloneDDS gl libcrypto libmongoc-1.0 sdl2 SDL2_mixer" >nul 2>&1
if not errorlevel 1 goto dependencies_ready
echo Installing dependencies, first run only. This takes a few minutes...
rem Running as WSL's root user is what avoids the sudo password prompt.
%WSLD% -u root --exec bash -c "export DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a; apt-get update && apt-get install -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold build-essential libgl1-mesa-dev libglfw3-dev cyclonedds-dev cyclonedds-tools pkg-config openssl libssl-dev libmongoc-dev libsdl2-dev libsdl2-mixer-dev"
if errorlevel 1 goto failed
:dependencies_ready

rem Show what this laptop runs, so setup problems are easy to spot.
echo Environment:
%WSLD% --exec bash -c "lsb_release -ds; echo CycloneDDS $(pkg-config --modversion CycloneDDS)"

rem ---- Build ----
rem sed undoes CRLF line endings, which stop .sh files running on Linux.
%WSLD% --cd "%~dp0." --exec bash -c "sed -i 's/\r$//' *.sh security/*.sh"
rem Close old copies first: Linux can't overwrite a program that is running.
%WSLD% --cd "%~dp0." --exec bash launch.sh stop
%WSLD% --cd "%~dp0." --exec bash launch.sh build
if errorlevel 1 goto failed
%WSLD% --cd "%~dp0." --exec bash launch.sh certs
if errorlevel 1 goto failed

rem ---- Launch ----
rem Terminal windows start minimized, so they can't cover the game or take its clicks.
rem Output goes to server.log, client1.log and client2.log.
if /i "%ROLE%"=="join" goto launch_join
if /i "%ROLE%"=="solo" goto launch_solo

:launch_host
start "Gingerbread Server" /min %WSLD% --cd "%~dp0." --exec bash -c "./server > server.log 2>&1"
ping -n 3 127.0.0.1 >nul
start "Gingerbread Player 1" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 1 > client1.log 2>&1"
echo Hosting. Player 2 can now start run.bat on their laptop and pick Join.
ping -n 4 127.0.0.1 >nul
exit /b 0

:launch_join
start "Gingerbread Player 2" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 2 > client2.log 2>&1"
exit /b 0

:launch_solo
start "Gingerbread Server" /min %WSLD% --cd "%~dp0." --exec bash -c "./server > server.log 2>&1"
ping -n 3 127.0.0.1 >nul
start "Gingerbread Player 1" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 1 > client1.log 2>&1"
start "Gingerbread Player 2" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 2 > client2.log 2>&1"
exit /b 0

:no_wsl
echo WSL is not installed. In an admin PowerShell run:
echo     wsl --install -d Ubuntu-24.04
echo     wsl --set-default Ubuntu-24.04
echo then restart and try again.
pause
exit /b 1

:distro_manual
echo.
echo Ubuntu 24.04 was installed but needs one manual first start.
echo Open "Ubuntu 24.04" from the Start menu, create a user, close it, then run this again.
pause
exit /b 1

:failed
echo.
echo Something failed - see the error above.
pause
exit /b 1
