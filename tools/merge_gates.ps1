# Merges the sub-agent gate results (study/INGEST.md, gates 2-4) into pack/verify/<unit>.gates.json.
#   tools\merge_gates.ps1 -Pack <course>\pack -Unit U01 -Blind a.json,b.json -Grounding g.json -Adversarial r.json
#   -Adversarial takes one file per independent reviewer (all must pass); -Adjudicated is the third pass's rulings.
# Blind answers for template instances (ids with '#') go under "templates"; everything else under "blind".
param([Parameter(Mandatory = $true)][string]$Pack, [Parameter(Mandatory = $true)][string]$Unit, [string[]]$Blind = @(), [string]$Grounding = "", [string[]]$Adversarial = @(), [string]$Adjudicated = "")
$ErrorActionPreference = 'Stop'
$out = [ordered]@{ blind = [ordered]@{}; templates = [ordered]@{}; grounding = [ordered]@{}; adversarial = [ordered]@{}; notes = [ordered]@{}; adjudicated = [ordered]@{} }
$path = Join-Path $Pack "verify\$Unit.gates.json"
if (Test-Path $path) {   # keep earlier results for items not in this run
    $old = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($k in 'blind', 'templates', 'grounding', 'adversarial', 'notes', 'adjudicated') { if ($old.$k) { foreach ($p in $old.$k.PSObject.Properties) { $out[$k][$p.Name] = $p.Value } } }
}
foreach ($b in $Blind) {
    $j = Get-Content $b -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($p in $j.answers.PSObject.Properties) { if ($p.Name -like '*#*') { $out.templates[$p.Name] = $p.Value } else { $out.blind[$p.Name] = $p.Value } }
    if ($j.notes) { foreach ($p in $j.notes.PSObject.Properties) { $out.notes["blind " + $p.Name] = $p.Value } }
}
if ($Grounding) { $j = Get-Content $Grounding -Raw -Encoding UTF8 | ConvertFrom-Json; foreach ($p in $j.grounding.PSObject.Properties) { $out.grounding[$p.Name] = $p.Value } }
# gate 4 is two independent reviewers when two files are given: an item passes only if every reviewer passes it
$seen = @{}
foreach ($a in $Adversarial) {
    $j = Get-Content $a -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($p in $j.adversarial.PSObject.Properties) {
        $v = $p.Value
        if ($seen.ContainsKey($p.Name)) {
            $prev = $out.adversarial[$p.Name]
            $ok = [bool]$prev.ok -and [bool]$v.ok
            $note = (@($prev.note, $v.note) | Where-Object { $_ }) -join ' | '
            $out.adversarial[$p.Name] = [ordered]@{ ok = $ok; note = $note }
        } else { $out.adversarial[$p.Name] = [ordered]@{ ok = [bool]$v.ok; note = [string]$v.note }; $seen[$p.Name] = 1 }
    }
    if ($j.templates) { foreach ($p in $j.templates.PSObject.Properties) { $out.notes["adversarial " + $p.Name] = $p.Value } }
}
# the third pass (a separate adjudicator with full access) rules on items a blind solver flagged
if ($Adjudicated) { $j = Get-Content $Adjudicated -Raw -Encoding UTF8 | ConvertFrom-Json; foreach ($p in $j.adjudicated.PSObject.Properties) { $out.adjudicated[$p.Name] = $p.Value } }
$json = $out | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($path, $json, (New-Object Text.UTF8Encoding $false))
"merged: $($out.blind.Count) blind, $($out.templates.Count) template samples, $($out.grounding.Count) grounding, $($out.adversarial.Count) adversarial -> $path"
