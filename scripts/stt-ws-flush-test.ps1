# STT WebSocket flush test — delegates to Node (works on Windows PowerShell 5+)
param(
    [string]$WsBase = "ws://127.0.0.1:9081",
    [int]$AudioSec = 2,
    [int]$FlushTimeoutSec = 180
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$nodeScript = Join-Path $scriptDir "stt-ws-flush-test.mjs"
node $nodeScript $WsBase $AudioSec $FlushTimeoutSec
exit $LASTEXITCODE
