# 10-step pipeline traffic test - watch Sankey at http://localhost:3001/d/voiceqas-pipeline-sankey
param(
    [string]$Base = "http://127.0.0.1:9080",
    [string]$Grafana = "http://127.0.0.1:3001",
    [int]$PauseSec = 3
)

$ErrorActionPreference = "Continue"
$read = @{ "X-Ops-Token" = "dev-read" }
$write = @{ "X-Ops-Token" = "dev-write" }
$auth = "Basic " + [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("admin:admin"))
$stamp = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$sessionRtp = "sankey-rtp-$stamp"

function New-PcmList {
    param(
        [int]$Seconds = 1,
        [int]$Rate = 16000,
        [int]$Freq = 440
    )
    $n = $Rate * $Seconds
    $list = [System.Collections.Generic.List[int]]::new()
    for ($i = 0; $i -lt $n; $i++) {
        $s = [int16](8000 * [math]::Sin(2 * [math]::PI * $Freq * $i / $Rate))
        [void]$list.Add($s -band 0xFF)
        [void]$list.Add(($s -shr 8) -band 0xFF)
    }
    return $list
}

function Get-Snap {
    Invoke-RestMethod -Uri "$Base/v1/ops/pipeline/snapshot" -Headers $read
}

function Get-GrafanaLinks {
    try {
        $body = '{"from":"now-30m","to":"now","queries":[{"refId":"A","datasource":{"type":"yesoreyeram-infinity-datasource","uid":"voiceqas-api"},"type":"json","source":"url","format":"table","url":"/v1/ops/pipeline/snapshot","url_options":{"method":"GET","params":[]},"root_selector":"","columns":[{"selector":"echarts","text":"echarts","type":"string"}],"parser":"backend"}]}'
        $r = Invoke-RestMethod -Uri "$Grafana/api/ds/query" -Method POST -Headers @{ Authorization = $auth; "Content-Type" = "application/json" } -Body $body
        $raw = $r.results.A.frames[0].data.values[0][0]
        if ($raw -is [string]) { ($raw | ConvertFrom-Json).series[0].links.Count } else { $raw.series[0].links.Count }
    } catch {
        -1
    }
}

function Show-Step {
    param(
        [int]$Num,
        [string]$Label,
        [string]$Status,
        [object]$Snap,
        [int]$GrafanaLinks
    )
    $sip = ($Snap.nodes | Where-Object { $_.name -eq "sip_in" }).metrics.bytes_out
    $rtp = ($Snap.nodes | Where-Object { $_.name -eq "rtp_ingress" }).metrics.bytes_out
    $vqa = ($Snap.nodes | Where-Object { $_.name -eq "vqa" }).metrics.bytes_out
    $asr = ($Snap.nodes | Where-Object { $_.name -eq "asr" }).metrics.bytes_out
    $links = $Snap.echarts.series[0].links.Count
    $score = ($Snap.nodes | Where-Object { $_.name -eq "vqa" }).metrics.composite_score
    [PSCustomObject]@{
        Test = $Num
        Step = $Label
        Status = $Status
        Links = $links
        Grafana = $GrafanaLinks
        SipIn = $sip
        Rtp = $rtp
        Vqa = $vqa
        Asr = $asr
        VqaScore = [math]::Round($score, 0)
    }
}

function Send-RtpUdp {
    param(
        [System.Collections.Generic.List[int]]$Pcm,
        [string]$Format = "rtp_g722"
    )
    $pack = Invoke-RestMethod -Uri "$Base/v1/tools/pack-rtp" -Method POST -ContentType "application/json" `
        -Body (@{ format = $Format; frame_ms = 20; pcm_bytes = $Pcm } | ConvertTo-Json -Compress)
    $udp = New-Object System.Net.Sockets.UdpClient
    foreach ($frame in $pack.frames) {
        $bytes = [byte[]]($frame | ForEach-Object { [byte]$_ })
        [void]$udp.Send($bytes, $bytes.Length, "127.0.0.1", 10000)
        Start-Sleep -Milliseconds 18
    }
    $udp.Close()
    return $pack.frames.Count
}

Write-Host ""
Write-Host "=== Sankey traffic test (10 steps) ===" -ForegroundColor Cyan
Write-Host "Dashboard: $Grafana/d/voiceqas-pipeline-sankey?var-session_id=`$__all"
Write-Host "Pause between steps: ${PauseSec}s - refresh Grafana manually or wait 30s"
Write-Host ""

$results = @()

function Run-Step {
    param(
        [int]$Num,
        [string]$Label,
        [scriptblock]$Action
    )
    $status = "OK"
    try {
        & $Action
    } catch {
        $status = "FAIL: $($_.Exception.Message)"
    }
    Start-Sleep -Seconds $PauseSec
    $snap = Get-Snap
    $gLinks = Get-GrafanaLinks
    $row = Show-Step -Num $Num -Label $Label -Status $status -Snap $snap -GrafanaLinks $gLinks
    $script:results += $row
    Write-Host ("[{0,2}] {1,-28} links={2} grafana={3} sip={4} rtp={5} vqa={6} asr={7} score={8}  {9}" -f `
        $row.Test, $row.Step, $row.Links, $row.Grafana, $row.SipIn, $row.Rtp, $row.Vqa, $row.Asr, $row.VqaScore, $row.Status)
}

Run-Step 1 "Baseline (no traffic)" { }

Run-Step 2 "VQA segment PCM 16k 1s" {
    $pcm = New-PcmList -Seconds 1 -Rate 16000 -Freq 440
    $body = @{
        format = "pcm_s16le_16k"
        sample_rate = 16000
        session_id = "sankey-t02-$stamp"
        pcm_bytes = $pcm
    } | ConvertTo-Json -Compress
    Invoke-RestMethod -Uri "$Base/v1/analyze/segment" -Method POST -ContentType "application/json" -Body $body | Out-Null
}

Run-Step 3 "STT segment PCM 16k 1s" {
    $pcm = New-PcmList -Seconds 1 -Rate 16000 -Freq 880
    $body = @{
        format = "pcm_s16le_16k"
        sample_rate = 16000
        language = "pt"
        pcm_bytes = $pcm
    } | ConvertTo-Json -Compress
    Invoke-RestMethod -Uri "$Base/v1/stt/transcribe/segment" -Method POST -ContentType "application/json" -Body $body | Out-Null
}

Run-Step 4 "VQA batch raw PCM 8k 1s" {
    $pcm = New-PcmList -Seconds 1 -Rate 8000 -Freq 523
    $bytes = [byte[]]($pcm | ForEach-Object { [byte]$_ })
    Invoke-RestMethod -Uri "$Base/v1/analyze/batch" -Method POST `
        -Headers @{ "X-Sample-Rate" = "8000"; "X-Audio-Format" = "pcm_s16le_8k"; "X-Session-Id" = "sankey-t04-$stamp" } `
        -ContentType "application/octet-stream" -Body $bytes | Out-Null
}

Run-Step 5 "STT batch raw PCM 16k 1s" {
    $pcm = New-PcmList -Seconds 1 -Rate 16000 -Freq 660
    $bytes = [byte[]]($pcm | ForEach-Object { [byte]$_ })
    Invoke-RestMethod -Uri "$Base/v1/stt/transcribe" -Method POST `
        -Headers @{ "X-Sample-Rate" = "16000"; "X-Audio-Format" = "pcm_s16le_16k"; "X-Language" = "pt"; "X-Session-Id" = "sankey-t05-$stamp" } `
        -ContentType "application/octet-stream" -Body $bytes | Out-Null
}

Run-Step 6 "Media session G.722 register" {
    $body = @{
        session_id = $sessionRtp
        format = "rtp_g722"
        sample_rate = 16000
    } | ConvertTo-Json
    Invoke-RestMethod -Uri "$Base/v1/media/sessions" -Method POST -Headers $write -ContentType "application/json" -Body $body | Out-Null
}

Run-Step 7 "RTP UDP G.722 2s -> :10000" {
    $pcm = New-PcmList -Seconds 2 -Rate 16000 -Freq 440
    $frames = Send-RtpUdp -Pcm $pcm -Format "rtp_g722"
    if ($frames -lt 10) { throw "only $frames RTP frames sent" }
}

Run-Step 8 "Agent PCM outbound" {
    $pcm = New-PcmList -Seconds 1 -Rate 16000 -Freq 330
    $bytes = [byte[]]($pcm | ForEach-Object { [byte]$_ })
    Invoke-RestMethod -Uri "$Base/v1/media/sessions/$sessionRtp/agent-audio" -Method POST `
        -Headers ($write + @{ "X-Sample-Rate" = "16000" }) -ContentType "application/octet-stream" -Body $bytes | Out-Null
}

Run-Step 9 "VQA segment PCM 16k 2s (bigger)" {
    $pcm = New-PcmList -Seconds 2 -Rate 16000 -Freq 440
    $body = @{
        format = "pcm_s16le_16k"
        sample_rate = 16000
        session_id = "sankey-t09-$stamp"
        pcm_bytes = $pcm
    } | ConvertTo-Json -Compress
    Invoke-RestMethod -Uri "$Base/v1/analyze/segment" -Method POST -ContentType "application/json" -Body $body | Out-Null
}

Run-Step 10 "RTP UDP PCMU 1s -> :10000" {
    $pcm = New-PcmList -Seconds 1 -Rate 8000 -Freq 440
    $frames = Send-RtpUdp -Pcm $pcm -Format "rtp_pcmu"
    if ($frames -lt 5) { throw "only $frames RTP frames sent" }
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
$results | Format-Table -AutoSize

$first = $results[0]
$last = $results[-1]
Write-Host ("Delta: links {0}->{1} | sip {2}->{3} | rtp {4}->{5} | vqa {6}->{7} | asr {8}->{9}" -f `
    $first.Links, $last.Links, $first.SipIn, $last.SipIn, $first.Rtp, $last.Rtp, $first.Vqa, $last.Vqa, $first.Asr, $last.Asr)

if ($last.Links -ge 10 -and $last.Grafana -ge 10) {
    Write-Host "PASS - Sankey has live pipeline data." -ForegroundColor Green
} elseif ($last.Links -gt $first.Links) {
    Write-Host "PARTIAL - bytes moved; open Grafana and hard-refresh (Ctrl+Shift+R)." -ForegroundColor Yellow
} else {
    Write-Host "WARN - little movement; check voiceqas/STT containers." -ForegroundColor Red
}

Write-Host ""
