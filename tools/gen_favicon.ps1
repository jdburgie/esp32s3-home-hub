# Regenerates firmware/homehub/favicon.h from the 32x32 badge PNG.
# The icon is embedded in the firmware rather than served off the SD card, so the
# dashboard still has its icon when no card is fitted.
#
#   pwsh tools/gen_favicon.ps1
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $repo "branding\threeoakwoods-badge-32.png"
$dst  = Join-Path $repo "firmware\homehub\favicon.h"

$b = [System.IO.File]::ReadAllBytes($src)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("// favicon.h -- Three Oak Woods badge, 32x32 PNG, served at /favicon.ico.")
[void]$sb.AppendLine("// GENERATED from branding/threeoakwoods-badge-32.png -- do not hand-edit.")
[void]$sb.AppendLine("// Regenerate: see tools/gen_favicon.ps1")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("#include <Arduino.h>")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("static const uint8_t FAVICON_PNG[] PROGMEM = {")
for ($i = 0; $i -lt $b.Length; $i += 16) {
  $chunk = $b[$i..([Math]::Min($i + 15, $b.Length - 1))]
  [void]$sb.AppendLine("  " + (($chunk | ForEach-Object { "0x{0:x2}" -f $_ }) -join ", ") + ",")
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("static const size_t FAVICON_PNG_LEN = sizeof(FAVICON_PNG);")
[System.IO.File]::WriteAllText($dst, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
Write-Host "wrote $dst ($($b.Length) PNG bytes embedded)"
