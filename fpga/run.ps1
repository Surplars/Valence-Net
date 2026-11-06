param(
 [Parameter(Mandatory=$true)][string]$RunDirectory,
 [ValidateSet('synth','route','bit')][string]$Phase='route',
 [string]$VivadoPath='E:\Xilinx\2025.2\Vivado\bin\vivado.bat'
)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).ProviderPath
$base=(Resolve-Path -LiteralPath 'E:\VM\Share\Valence-rtl').ProviderPath
if(-not $run.StartsWith($base+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
 throw 'Vivado run must be within valence-rtl'
}
if(Test-Path -LiteralPath (Join-Path $run 'vivado.log')){throw 'This frozen run has already started'}
$manifest=Get-Content -LiteralPath (Join-Path $run 'provenance.json') -Raw | ConvertFrom-Json
foreach($entry in $manifest.input_sha256.PSObject.Properties){
 $actual=(Get-FileHash -LiteralPath (Join-Path $run $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
 if($actual -ne $entry.Value){throw "Frozen input hash mismatch: $($entry.Name)"}
}
Push-Location $run
try {
 $previous=$ErrorActionPreference
 $ErrorActionPreference='Continue'
 & $VivadoPath -mode batch -notrace -source (Join-Path $run 'build.tcl') -log vivado.log -journal vivado.jou -tclargs $run $Phase *> console.log
 $code=$LASTEXITCODE
 $ErrorActionPreference=$previous
 if($code -ne 0){throw "Vivado failed; inspect $run\console.log"}
} finally {Pop-Location}
Write-Output $run
