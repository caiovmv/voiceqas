# Build Release "badass": LTO + -march=x86-64-v3 nos targets voiceqas (nao sherpa-onnx).
# Rodar manualmente antes de testes finais — link LTO e mais lento que build normal.
#
# Uso:
#   .\scripts\build-badass.ps1
#   .\scripts\build-badass.ps1 -Local
#   .\scripts\build-badass.ps1 -Redeploy
param(
    [switch]$Local,
    [switch]$Redeploy
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location ..

if ($Local) {
    Write-Host ">> build-badass (local): LTO + x86-64-v3"
    $env:VOICEQAS_PERF_BADASS = "ON"
    if (Get-Command bash -ErrorAction SilentlyContinue) {
        bash ./scripts/build.sh -DVOICEQAS_PERF_BADASS=ON
    } else {
        throw "bash nao encontrado — use Docker ou WSL para build local"
    }
    exit $LASTEXITCODE
}

Write-Host ">> build-badass (docker): LTO + x86-64-v3 — link pode levar varios minutos"
docker compose build voiceqas --build-arg VOICEQAS_PERF_BADASS=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

docker tag voiceqas:local voiceqas:badass-local 2>$null
Write-Host ">> image: voiceqas:local (tag extra: voiceqas:badass-local)"

if ($Redeploy) {
    Write-Host ">> redeploy voiceqas + beyla + command-center"
    docker compose up -d --force-recreate voiceqas command-center beyla
    Start-Sleep -Seconds 5
    $health = docker inspect voiceqas --format "{{.State.Health.Status}}"
    Write-Host "voiceqas health=$health"
}

Write-Host "OK — flags: Release -O3, LTO (IPO), -march=x86-64-v3 (sem -ffast-math)"