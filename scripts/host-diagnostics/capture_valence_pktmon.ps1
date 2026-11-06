param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator rights are required by the Windows PktMon driver' }
$allowed='C:\Users\surplars\Documents\Codex\2026-10-23\https-github-com-surplars-valence-tree-3\outputs\'
$directory=[IO.Path]::GetFullPath($OutputDirectory)
if (-not $directory.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected capture output directory' }
$null=New-Item -ItemType Directory -Path $directory -Force
$utf8=[Text.UTF8Encoding]::new($false)
$filterAdded=$false;$captureStarted=$false
function Run-Pktmon([string[]]$Arguments) {
 $start=[Diagnostics.ProcessStartInfo]::new()
 $start.FileName="$env:WINDIR\System32\pktmon.exe"
 $start.UseShellExecute=$false; $start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
 $start.StandardOutputEncoding=[Text.Encoding]::UTF8; $start.StandardErrorEncoding=[Text.Encoding]::UTF8
 foreach($argument in $Arguments){if($argument -match '["\r\n]' -or $argument.EndsWith('\')){throw 'Unexpected native argument'}}
 $start.Arguments=($Arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start;$null=$process.Start()
 $stdout=$process.StandardOutput.ReadToEnd();$stderr=$process.StandardError.ReadToEnd();$process.WaitForExit();$code=$process.ExitCode;$process.Dispose()
 $text=$stdout+$stderr
 [IO.File]::AppendAllText((Join-Path $directory 'pktmon-commands.log'),(('pktmon '+($Arguments -join ' '))+[Environment]::NewLine+$text+[Environment]::NewLine),$utf8)
 if($code){throw "pktmon failed ($code): $($Arguments -join ' ')"};return $text
}
try {
 $state=Run-Pktmon @('status')
 if ($state -notmatch '(?i)not running|stopped|未运行|已停止|没有运行|尚未启动|未启动') { throw 'PktMon state is active or unrecognized; no capture settings changed' }
 $filters=Run-Pktmon @('filter','list')
 if ($filters -notmatch '(?i)no\s+(?:packet\s+)?filters|未.*筛选器|没有.*筛选器|无.*筛选器|(?m:^\s*无\s*$)') { throw 'Existing filters or unrecognized output; no filters changed' }
 $null=Run-Pktmon @('filter','add','ValenceIperfRx256','-i','192.168.137.10','-t','TCP','-p','5201');$filterAdded=$true
 $null=Run-Pktmon @('start','--capture','--comp','nics','--pkt-size','128','--flags','0x12','--file-name',(Join-Path $directory 'capture.etl'),'--file-size','128','--log-mode','multi-file');$captureStarted=$true
 [IO.File]::WriteAllText((Join-Path $directory 'READY'),[DateTime]::UtcNow.ToString('o'),$utf8)
 $deadline=[DateTime]::UtcNow.AddSeconds(25)
 while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath (Join-Path $directory 'STOP'))) { Start-Sleep -Milliseconds 100 }
} catch { [IO.File]::WriteAllText((Join-Path $directory 'FAILED.txt'),($_|Out-String),$utf8);throw }
finally {
 if ($captureStarted) { $null=Run-Pktmon @('stop') }
 if ($filterAdded) { $null=Run-Pktmon @('filter','remove','ValenceIperfRx256') }
}
foreach ($file in Get-ChildItem -LiteralPath $directory -Filter '*.etl') { $null=Run-Pktmon @('etl2pcap',$file.FullName,'--out',([IO.Path]::ChangeExtension($file.FullName,'pcapng'))) }
[IO.File]::WriteAllText((Join-Path $directory 'DONE'),[DateTime]::UtcNow.ToString('o'),$utf8)