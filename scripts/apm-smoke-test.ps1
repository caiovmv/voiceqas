# APM smoke test: generate REST traffic and verify traces in Tempo.
param(
    [string]$Base = "http://127.0.0.1:9080",
    [string]$Tempo = "http://127.0.0.1:3200",
    [int]$Requests = 20
)

$ErrorActionPreference = "Stop"
$read = @{ "X-Ops-Token" = "dev-read" }

Write-Host "Generating $Requests REST requests..."
for ($i = 0; $i -lt $Requests; $i++) {
    Invoke-RestMethod -Uri "$Base/health" -Headers $read | Out-Null
    Invoke-RestMethod -Uri "$Base/v1/stt/ready" | Out-Null
    Invoke-RestMethod -Uri "$Base/v1/ops/pipeline/snapshot" -Headers $read | Out-Null
    Start-Sleep -Milliseconds 100
}

Write-Host "Waiting for trace ingest..."
Start-Sleep -Seconds 8

$end = [int64]([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
$start = $end - 600
$searchUrl = "$Tempo/api/search?start=$start&end=$end&limit=10"
$search = Invoke-RestMethod -Uri $searchUrl

$count = @($search.traces).Count
Write-Host "Tempo traces (last 10 min): $count"
if ($count -gt 0) {
    $search.traces | Select-Object -First 5 | ForEach-Object {
        Write-Host "  - $($_.traceID) root=$($_.rootServiceName) durationMs=$($_.durationMs)"
    }
    Write-Host "PASS - open Grafana APM: http://localhost:3001/d/voiceqas-apm"
    exit 0
}

Write-Host "FAIL - no traces in Tempo. Check: docker logs voiceqas-beyla"
exit 1
