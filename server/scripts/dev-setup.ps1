# Local Cosmic Online server for development (Windows PowerShell): no Cloudflare account needed.
#   powershell -ExecutionPolicy Bypass -File scripts\dev-setup.ps1 [-Levels]
param([switch]$Levels)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

npm ci
if (-not (Test-Path ".dev.vars")) {
    $rand = { param($n) -join ((1..$n) | ForEach-Object { '{0:x2}' -f (Get-Random -Maximum 256) }) }
    "IP_HASH_SALT=$(& $rand 24)`nADMIN_TOKEN=$(& $rand 32)`n" | Set-Content -NoNewline -Encoding ascii ".dev.vars"
    Write-Host ".dev.vars créé (secrets locaux aléatoires)."
}
npx wrangler d1 migrations apply cosmic-online-dev --local --env dev

if ($Levels) {
    $game = if ($env:COSMIC_GAME_BIN) { $env:COSMIC_GAME_BIN } else { "..\build\bin\Release\CosmicPlatformer.exe" }
    if (-not (Test-Path $game)) { $game = "..\build\bin\CosmicPlatformer.exe" }
    $manifest = Join-Path $env:TEMP "cosmic_manifest.json"
    & $game --online-manifest $manifest
    $server = Start-Process npx -ArgumentList "wrangler dev --env dev --port 8787 --ip 127.0.0.1" -PassThru -NoNewWindow
    for ($i = 0; $i -lt 60; $i++) {
        try { Invoke-RestMethod http://127.0.0.1:8787/api/v1/health | Out-Null; break } catch { Start-Sleep 1 }
    }
    node scripts/register-levels.mjs $manifest http://127.0.0.1:8787
    Write-Host "Niveaux enregistrés. Serveur local : http://127.0.0.1:8787"
    Wait-Process -Id $server.Id
} else {
    npx wrangler dev --env dev --port 8787 --ip 127.0.0.1
}
