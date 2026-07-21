# Regenerates the embedded web assets in firmware/homehub/ from branding/.
# Assets are compiled into the firmware rather than served off the SD card, so
# the dashboard still looks right with no card fitted.
#
#   pwsh tools/gen_assets.ps1
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

function Write-ByteArrayHeader {
  param([string]$Src, [string]$Dst, [string]$Symbol, [string]$Note)
  $b = [System.IO.File]::ReadAllBytes($Src)
  $name = Split-Path -Leaf $Src
  $sb = New-Object System.Text.StringBuilder
  [void]$sb.AppendLine("// $(Split-Path -Leaf $Dst) -- $Note")
  [void]$sb.AppendLine("// GENERATED from branding/$name -- do not hand-edit.")
  [void]$sb.AppendLine("// Regenerate: pwsh tools/gen_assets.ps1")
  [void]$sb.AppendLine("#pragma once")
  [void]$sb.AppendLine("#include <Arduino.h>")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("static const uint8_t $Symbol`[] PROGMEM = {")
  for ($i = 0; $i -lt $b.Length; $i += 16) {
    $chunk = $b[$i..([Math]::Min($i + 15, $b.Length - 1))]
    [void]$sb.AppendLine("  " + (($chunk | ForEach-Object { "0x{0:x2}" -f $_ }) -join ", ") + ",")
  }
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("static const size_t ${Symbol}_LEN = sizeof($Symbol);")
  [System.IO.File]::WriteAllText($Dst, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
  Write-Host "wrote $Dst ($($b.Length) bytes embedded)"
}

Write-ByteArrayHeader `
  -Src (Join-Path $repo "branding\threeoakwoods-badge-32.png") `
  -Dst (Join-Path $repo "firmware\homehub\favicon.h") `
  -Symbol "FAVICON_PNG" `
  -Note "Three Oak Woods badge, 32x32 PNG, served at /favicon.ico."

Write-ByteArrayHeader `
  -Src (Join-Path $repo "branding\threeoakwoods-logo.svg") `
  -Dst (Join-Path $repo "firmware\homehub\logo.h") `
  -Symbol "LOGO_SVG" `
  -Note "Three Oak Woods badge, vector, served at /logo.svg for the header."
