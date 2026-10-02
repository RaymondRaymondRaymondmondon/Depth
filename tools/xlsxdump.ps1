# Dumps every sheet of an .xlsx workbook as tab-separated text (no Excel needed: the workbook is a zip of XML).
#   tools\xlsxdump.ps1 -Xlsx <file> [-Out <dir>]    -> <dir>\<workbook>__<sheet>.tsv
param([Parameter(Mandatory = $true)][string]$Xlsx, [string]$Out = "")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$path = (Resolve-Path $Xlsx).Path
$name = [IO.Path]::GetFileNameWithoutExtension($path)
if ($Out -eq "") { $Out = Split-Path $path }
New-Item -ItemType Directory -Force $Out | Out-Null
$zip = [IO.Compression.ZipFile]::OpenRead($path)
function ReadEntry($e) { $sr = New-Object IO.StreamReader($e.Open()); $t = $sr.ReadToEnd(); $sr.Close(); $t }
$ns = @{ m = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'; r = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships' }
# shared strings
$shared = @()
$sse = $zip.GetEntry('xl/sharedStrings.xml')
if ($sse) {
    [xml]$sx = ReadEntry $sse
    foreach ($si in $sx.sst.si) {
        $txt = ""
        if ($si.t) { $txt = [string]$si.t.'#text'; if (-not $txt) { $txt = [string]$si.t } }
        if ($si.r) { foreach ($run in $si.r) { $txt += [string]$run.t.'#text'; if (-not $run.t.'#text') { $txt += [string]$run.t } } }
        $shared += $txt
    }
}
# sheet names -> files
[xml]$wb = ReadEntry $zip.GetEntry('xl/workbook.xml')
[xml]$rels = ReadEntry $zip.GetEntry('xl/_rels/workbook.xml.rels')
$relMap = @{}
foreach ($rl in $rels.Relationships.Relationship) { $relMap[$rl.Id] = $rl.Target }
function ColIndex($ref) { $letters = ($ref -replace '[0-9]', ''); $n = 0; foreach ($ch in $letters.ToCharArray()) { $n = $n * 26 + ([int][char]$ch - 64) }; $n }
foreach ($sh in $wb.workbook.sheets.sheet) {
    $rid = $sh.GetAttribute('id', $ns.r)
    $target = $relMap[$rid]; $target = $target -replace '^/', ''; if ($target -notmatch '^xl/') { $target = 'xl/' + $target }
    $entry = $zip.GetEntry($target)
    if (-not $entry) { continue }
    [xml]$sx = ReadEntry $entry
    $lines = @()
    foreach ($row in $sx.worksheet.sheetData.row) {
        $cells = @{}; $max = 0
        foreach ($c in $row.c) {
            $ci = ColIndex $c.r
            $v = ""
            if ($c.t -eq 's') { $v = $shared[[int]$c.v] }
            elseif ($c.t -eq 'inlineStr') { $v = [string]$c.is.t.'#text'; if (-not $v) { $v = [string]$c.is.t } }
            elseif ($c.v) { $v = [string]$c.v }
            $v = $v -replace "`t", ' ' -replace "`r?`n", ' / '
            $cells[$ci] = $v; if ($ci -gt $max) { $max = $ci }
        }
        $parts = @(); for ($i = 1; $i -le $max; $i++) { $parts += $(if ($cells.ContainsKey($i)) { $cells[$i] } else { "" }) }
        $lines += ($parts -join "`t")
    }
    $safe = ($sh.name -replace '[^A-Za-z0-9_-]', '_')
    $file = Join-Path $Out ("{0}__{1}.tsv" -f $name, $safe)
    [IO.File]::WriteAllLines($file, $lines, (New-Object Text.UTF8Encoding $false))
    "{0}: {1} rows -> {2}" -f $sh.name, $lines.Count, $file
}
$zip.Dispose()
