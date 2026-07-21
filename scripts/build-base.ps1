# Build voiceqas-build-base (toolchain + vcpkg deps). Run rarely:
# only when vcpkg.json / vcpkg-configuration.json / baseline change.
param(
    [string]$Tag = "voiceqas-build-base:local",
    [string]$BuildTests = "OFF"
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location ..

Write-Host "Building $Tag (VOICEQAS_BUILD_TESTS=$BuildTests) ..."
docker build -f Dockerfile.base -t $Tag --build-arg "VOICEQAS_BUILD_TESTS=$BuildTests" .
Write-Host "OK — base ready. App rebuilds: docker compose build voiceqas"