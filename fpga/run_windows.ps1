param(
    [Parameter(Mandatory=$true)][string]$SourceRun,
    [ValidateSet('Stage','Run','Collect')][string]$Mode = 'Stage',
    [ValidateSet('synth','route')][string]$Phase = 'route',
    [string]$VivadoPath = 'E:\Xilinx\2025.2\Vivado\bin\vivado.bat'
)
$ErrorActionPreference = 'Stop'
$sourcePath = (Resolve-Path -LiteralPath $SourceRun).ProviderPath
$provenancePath = Join-Path $sourcePath 'provenance.json'
if (-not (Test-Path -LiteralPath $provenancePath)) { throw 'Missing frozen RTL provenance' }
$runName = Split-Path -Leaf $sourcePath
if ($runName -notmatch '^\d{8}T\d{6}Z$') { throw 'Unexpected snapshot directory name' }
$targetPath = Join-Path 'E:\VM\Share\Valence-rtl' ('hardware-netstack-ooc-' + $runName)
$markerPath = Join-Path $targetPath '.netstack-source.txt'
if ($Mode -eq 'Collect') {
    if (-not (Test-Path -LiteralPath $markerPath)) { throw 'Stage this snapshot first' }
    if ((Resolve-Path -LiteralPath (Get-Content -LiteralPath $markerPath -Raw).Trim()).ProviderPath -ne $sourcePath) { throw 'Destination belongs to a different snapshot' }
    if ((Get-Content -LiteralPath (Join-Path $targetPath 'progress.txt') -Raw).Trim() -ne 'completed') {
        throw 'Vivado run is not complete; active results must not be copied as final'
    }
    $provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
    foreach ($top in $provenance.targets.PSObject.Properties.Name) {
        Copy-Item -LiteralPath (Join-Path $targetPath $top) -Destination $sourcePath -Recurse -Force
    }
    foreach ($name in @('results.tsv','progress.txt','vivado.log','vivado.jou','console.log','summary.json')) {
        $filePath = Join-Path $targetPath $name
        if (Test-Path -LiteralPath $filePath) { Copy-Item -LiteralPath $filePath -Destination $sourcePath -Force }
    }
} else {
    if (Test-Path -LiteralPath $targetPath) { throw "Refusing to overwrite existing run: $targetPath" }
    New-Item -ItemType Directory -Path $targetPath | Out-Null
    Set-Content -LiteralPath $markerPath -Value $sourcePath -Encoding UTF8
    Copy-Item -LiteralPath (Join-Path $sourcePath 'input') -Destination $targetPath -Recurse
    foreach ($name in @('ooc.tcl','provenance.json','targets.txt')) {
        $filePath = Join-Path $sourcePath $name
        if (Test-Path -LiteralPath $filePath) { Copy-Item -LiteralPath $filePath -Destination $targetPath }
    }
    if ($Mode -eq 'Run') {
        Push-Location $targetPath
        try {
            # Windows PowerShell wraps native stderr as ErrorRecords. Keep
            # draining both streams; native exit status and completed reports
            # determine success, and preserve all messages for later audit.
            $previousPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                & $VivadoPath -mode batch -notrace -source (Join-Path $targetPath 'ooc.tcl') -log (Join-Path $targetPath 'vivado.log') -journal (Join-Path $targetPath 'vivado.jou') -tclargs $targetPath $Phase *> (Join-Path $targetPath 'console.log')
                $vivadoExitCode = $LASTEXITCODE
            } finally { $ErrorActionPreference = $previousPreference }
            if ($vivadoExitCode -ne 0) { throw "Vivado exited with code $vivadoExitCode; inspect $targetPath" }
            if (-not (Test-Path -LiteralPath (Join-Path $targetPath 'progress.txt')) -or
                (Get-Content -LiteralPath (Join-Path $targetPath 'progress.txt') -Raw).Trim() -ne 'completed') {
                throw "Vivado did not complete. Inspect $targetPath"
            }
        } finally { Pop-Location }
    }
}
Write-Output $targetPath
