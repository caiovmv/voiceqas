# Recreate Beyla after voiceqas restarts (shared PID namespace).
param(
    [switch]$RedeployVoiceqas
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path) | Out-Null
Set-Location ..

if ($RedeployVoiceqas) {
    docker compose up -d voiceqas
    docker compose ps voiceqas
}

docker compose up -d --force-recreate beyla
Start-Sleep -Seconds 5

$state = docker inspect voiceqas-beyla --format "{{.State.Status}} exit={{.State.ExitCode}}"
Write-Host "voiceqas-beyla: $state"
if ($state -notmatch "^running") {
    docker logs voiceqas-beyla --tail 20
    exit 1
}

docker logs voiceqas-beyla --tail 5
Write-Host "OK - Beyla attached to voiceqas"
