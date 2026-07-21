# Redeploy voiceqas + command-center + Beyla (shared PID namespace).
# Sempre recrie Beyla junto com voiceqas, senao RED REST no Prometheus fica vazio.
#
# Code-only rebuild (fast):  .\scripts\redeploy-voiceqas.ps1 -Build
# Deps/toolchain:            .\scripts\build-base.ps1   # once / when vcpkg.json changes
param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path) | Out-Null
Set-Location ..

if ($Build) {
    $base = docker image inspect voiceqas-build-base:local 2>$null
    if (-not $base) {
        Write-Host "voiceqas-build-base:local nao existe. Rode: .\scripts\build-base.ps1"
        exit 1
    }
}

$dockerArgs = @("compose", "up", "-d", "--force-recreate", "voiceqas", "command-center", "beyla")
if ($Build) {
    $dockerArgs = @("compose", "up", "-d", "--build", "--force-recreate", "voiceqas", "command-center", "beyla")
}

docker @dockerArgs
Start-Sleep -Seconds 5

$vq = docker inspect voiceqas --format "{{.State.Health.Status}}"
$beyla = docker inspect voiceqas-beyla --format "{{.State.Status}}"
Write-Host "voiceqas health=$vq  beyla=$beyla"
if ($beyla -ne "running") {
    docker logs voiceqas-beyla --tail 25
    exit 1
}
Write-Host "OK — use este script apos mudancas em voiceqas (inclui Beyla sidecar)"