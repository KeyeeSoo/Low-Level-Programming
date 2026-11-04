@echo off
rem The Gingerbread Curse: double-click to play.
rem   run.bat        asks which role this laptop plays
rem   run.bat host   this laptop runs the server and Player 1
rem   run.bat join   this laptop is Player 2 and connects to the host
rem   run.bat solo   server and both players on this laptop (testing)
rem Everything runs in WSL's Ubuntu 24.04, installed here if it is missing, so
rem every laptop has the same CycloneDDS (different versions can't connect).
rem The host hands its DDS Security certificates to joiners over the network.
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
rem Packages = Week 1 slide 8, plus pkg-config (build.sh needs it), openssl and
rem libssl-dev (DDS encryption certificates, password hashing) and libmongoc-dev
rem (MongoDB accounts, used only when a .env file is present).
%WSLD% --exec bash -c "command -v idlc && command -v python3 && pkg-config --exists glfw3 CycloneDDS gl libcrypto libmongoc-1.0" >nul 2>&1
if not errorlevel 1 goto dependencies_ready
echo Installing dependencies, first run only. This takes a few minutes...
rem Running as WSL's root user is what avoids the sudo password prompt.
%WSLD% -u root --exec bash -c "export DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a; apt-get update && apt-get install -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold libgl1-mesa-dev libglfw3-dev cyclonedds-dev cyclonedds-tools pkg-config openssl libssl-dev libmongoc-dev python3"
if errorlevel 1 goto failed
:dependencies_ready

rem ---- Build ----
rem sed undoes CRLF line endings, which stop .sh files running on Linux.
%WSLD% --cd "%~dp0." --exec bash -c "sed -i 's/\r$//' *.sh security/*.sh security/*.py"
%WSLD% --cd "%~dp0." --exec bash launch.sh build
if errorlevel 1 goto failed
%WSLD% --cd "%~dp0." --exec bash launch.sh stop

rem ---- Launch ----
rem Terminal windows start minimized, so they can't cover the game or take its clicks.
rem Output goes to server.log, client1.log, client2.log (and certs.log on the host).
if /i "%ROLE%"=="join" goto launch_join
if /i "%ROLE%"=="solo" goto launch_solo

:launch_host
%WSLD% --cd "%~dp0." --exec bash launch.sh host-certs
if errorlevel 1 goto failed
start "Gingerbread Server" /min %WSLD% --cd "%~dp0." --exec bash launch.sh server-host
ping -n 3 127.0.0.1 >nul
start "Gingerbread Player 1" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 1 > client1.log 2>&1"
echo Hosting. Player 2 can now start run.bat and pick Join.
ping -n 4 127.0.0.1 >nul
exit /b 0

:launch_join
%WSLD% --cd "%~dp0." --exec bash launch.sh fetch
if errorlevel 1 goto failed
start "Gingerbread Player 2" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 2 > client2.log 2>&1"
exit /b 0

:launch_solo
%WSLD% --cd "%~dp0." --exec bash launch.sh host-certs
if errorlevel 1 goto failed
start "Gingerbread Server" /min %WSLD% --cd "%~dp0." --exec bash launch.sh server-solo
ping -n 3 127.0.0.1 >nul
start "Gingerbread Player 1" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 1 > client1.log 2>&1"
start "Gingerbread Player 2" /min %WSLD% --cd "%~dp0." --exec bash -c "./client 2 > client2.log 2>&1"
exit /b 0

:no_wsl
echo WSL is not installed. Run "wsl --install" in an admin PowerShell, restart, then try again.
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
