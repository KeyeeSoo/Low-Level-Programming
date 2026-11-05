# Opens the game's DDS ports for WSL while the game runs, then closes them.
# Started by run.bat (Host and Join). It asks for admin once (the UAC prompt),
# adds ONE inbound rule, waits until launch.sh writes .game_closed_<token>
# (all game windows closed), then removes the rule again. The rest of the
# firewall stays on, on Wi-Fi and hotspot alike.
#   powershell -ExecutionPolicy Bypass -File firewall.ps1 -Close
# removes any leftover game rules by hand.
param([string]$Token = '0', [switch]$Close)

$Prefix = 'GingerbreadCurse-DDS'
$WslVmCreatorId = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'  # WSL's ID in the Hyper-V firewall
$Ports = '7400-7449'  # matches CYCLONEDDS_URI in launch.sh

$me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Token', $Token)
    if ($Close) { $argList += '-Close' }
    try {
        Start-Process powershell -Verb RunAs -WindowStyle Hidden -ArgumentList $argList -ErrorAction Stop
    } catch {
        exit 1  # UAC prompt declined
    }
    if ($Close) { exit 0 }
    # Don't let run.bat start the server (and logins) until the rule exists.
    $ready = Join-Path $PSScriptRoot ".fw_ready_$Token"
    for ($i = 0; $i -lt 60 -and -not (Test-Path -LiteralPath $ready); $i++) { Start-Sleep -Milliseconds 500 }
    $ok = Test-Path -LiteralPath $ready
    Remove-Item -LiteralPath $ready -ErrorAction SilentlyContinue
    if ($ok) { exit 0 } else { exit 1 }
}

# Mirrored WSL networking (Windows 11 22H2+) is filtered by the Hyper-V firewall.
if (-not (Get-Command New-NetFirewallHyperVRule -ErrorAction SilentlyContinue)) {
    if (-not $Close) { New-Item -ItemType File -Force -Path (Join-Path $PSScriptRoot ".fw_ready_$Token") | Out-Null }
    exit 0
}

function Remove-GameRules([string]$Pattern) {
    Get-NetFirewallHyperVRule -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like $Pattern } |
        ForEach-Object { Remove-NetFirewallHyperVRule -Name $_.Name -ErrorAction SilentlyContinue }
}

if ($Close) { Remove-GameRules "$Prefix-*"; exit 0 }

# run.bat already closed any older game, so older rules are leftovers.
Remove-GameRules "$Prefix-*"
$name = "$Prefix-$Token"
New-NetFirewallHyperVRule -Name $name -DisplayName 'Gingerbread Curse DDS (only while playing)' `
    -Direction Inbound -VMCreatorId $WslVmCreatorId -Protocol UDP -LocalPorts $Ports -Action Allow | Out-Null
New-Item -ItemType File -Force -Path (Join-Path $PSScriptRoot ".fw_ready_$Token") | Out-Null  # tells run.bat to go on

# Wait for launch.sh to say every game window has closed (at most 12 hours).
$signal = Join-Path $PSScriptRoot ".game_closed_$Token"
$deadline = (Get-Date).AddHours(12)
while (-not (Test-Path -LiteralPath $signal) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 3 }
Remove-Item -LiteralPath $signal -ErrorAction SilentlyContinue
Remove-GameRules $name
