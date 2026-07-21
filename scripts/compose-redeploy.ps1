# Aguarda Docker Engine e executa: down -> build -> up -d
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root

Write-Host ">> Aguardando Docker Engine..."
$deadline = (Get-Date).AddMinutes(10)
$ready = $false
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'SilentlyContinue'
while ((Get-Date) -lt $deadline) {
    & docker info *> $null
    if ($LASTEXITCODE -eq 0) {
        $ready = $true
        Write-Host ">> Docker pronto."
        break
    }
    Start-Sleep -Seconds 5
}
$ErrorActionPreference = $prevEap
if (-not $ready) {
    Write-Error "Docker Engine nao respondeu em 10 min. Abra o Docker Desktop e aguarde 'Engine running'."
    exit 1
}

Write-Host ">> docker compose down"
docker compose down
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ">> docker compose build"
docker compose build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ">> docker compose up -d"
docker compose up -d
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ">> Status:"
docker compose ps
Write-Host ">> URLs: http://localhost:9080/health | http://localhost:3000 | http://localhost:3001"
