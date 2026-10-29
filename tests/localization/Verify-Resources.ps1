$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$rc=[IO.File]::ReadAllText((Join-Path $repoRoot 'IPRegionMon.rc'))
$tables=[regex]::Matches($rc,'(?s)STRINGTABLE\s*BEGIN(.*?)END')
if ($tables.Count -ne 2) { throw 'Expected the existing ZH and EN string tables.' }
$values=@()
foreach ($table in $tables) {
    $strings=@{}
    foreach ($entry in [regex]::Matches($table.Groups[1].Value,'(?m)^\s*(IDS_\w+)\s+("(?:[^"\\]|\\.)*")')) {
        $strings[$entry.Groups[1].Value]=$entry.Groups[2].Value | ConvertFrom-Json
    }
    $values+=,$strings
}
$zhIds=($values[0].Keys | Sort-Object) -join ','
$enIds=($values[1].Keys | Sort-Object) -join ','
if ($zhIds -cne $enIds) { throw 'EN/ZH resource IDs differ.' }
foreach ($id in $values[0].Keys) {
    if (!$values[0][$id] -or !$values[1][$id]) { throw "Empty localized resource: $id" }
    if ($values[1][$id] -match '[\u4e00-\u9fff]') { throw "Chinese UI text in English resource: $id" }
    $pattern='%(?:[-+ #0]*\d*(?:\.\d+)?)(?:ll|l)?[sdfu]'
    $zh=([regex]::Matches($values[0][$id],$pattern) | ForEach-Object Value) -join ','
    $en=([regex]::Matches($values[1][$id],$pattern) | ForEach-Object Value) -join ','
    if ($zh -cne $en) { throw "Format arguments differ: $id" }
}
# Persisted target names and provider-data normalization are data, not UI labels.
$dataLiterals=@('百度','默认','中国电信','中国联通','中国移动','教育网','科技网')
foreach ($name in @('DataManager.cpp','OptionsDlg.cpp','CustomDrawItem.cpp','IPRegionMon.cpp','ModernUI.cpp')) {
    $source=[IO.File]::ReadAllText((Join-Path $repoRoot $name))
    foreach ($literal in [regex]::Matches($source,'L"(?:[^"\\]|\\.)*"')) {
        $text=$literal.Value.Substring(1) | ConvertFrom-Json
        if ($text -match '[\u4e00-\u9fff]') {
            if ($name -eq 'DataManager.cpp' -and $text -cin $dataLiterals) { continue }
            # Replaced by the taskbar-width change; this sample is never displayed.
            if ($name -eq 'CustomDrawItem.cpp' -and $text -ceq 'CN 上海 (220.181.38.148)') { continue }
            throw "Unlocalized C++ UI literal in $name : $text"
        }
    }
}
Write-Host "PASS: $($values[0].Count) EN/ZH resource pairs, format arguments, English text and C++ literal inventory"
