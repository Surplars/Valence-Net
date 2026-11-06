param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath($OutputDirectory)
$allowed='C:\Users\surplars\Documents\Codex\2026-10-23\https-github-com-surplars-valence-tree-3\outputs\'
if(-not $directory.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected evidence root'}
$null=New-Item -ItemType Directory -Path $directory -Force
$utf8=[Text.UTF8Encoding]::new($false);$original=$null;$restoreNeeded=$false
function Save-State([string]$Name,$Value){[IO.File]::WriteAllText((Join-Path $directory $Name),($Value|ConvertTo-Json -Depth 6),$utf8)}
function Check-Adapter {
 $adapter=Get-NetAdapter -Name '以太网'
 if("$($adapter.InterfaceGuid)" -ne '{26AC5CC1-B219-4A90-8EB6-9F3D3A63C361}' -or $adapter.InterfaceDescription -ne 'Realtek PCIe GbE Family Controller'){throw 'Adapter identity mismatch'}
 return $adapter
}
try {
 Start-Transcript -Path (Join-Path $directory 'elevated-transcript.txt') -Force|Out-Null
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
 if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator token required'}
 $adapter=Check-Adapter
 $before=@(Get-NetAdapterAdvancedProperty -Name $adapter.Name|Select-Object RegistryKeyword,RegistryValue,DisplayName,DisplayValue)
 Save-State 'all-properties-before.json' $before
 $property=Get-NetAdapterAdvancedProperty -Name $adapter.Name -RegistryKeyword '*InterruptModeration'
 $original=@($property.RegistryValue)
 if(($original -join ',') -ne '1'){throw 'Expected original interrupt moderation value 1; no change made'}
 Save-State 'change.json' ([ordered]@{interface_guid="$($adapter.InterfaceGuid)";adapter_name=$adapter.Name;keyword='*InterruptModeration';original=$original;temporary=@('0');started_utc=[DateTime]::UtcNow.ToString('o');max_seconds=180})
 $restoreNeeded=$true
 Set-NetAdapterAdvancedProperty -Name $adapter.Name -RegistryKeyword '*InterruptModeration' -RegistryValue @('0') -NoRestart
 Restart-NetAdapter -Name $adapter.Name -Confirm:$false
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 do {$adapter=Check-Adapter;if($adapter.Status -eq 'Up'){break};Start-Sleep -Milliseconds 500}while([DateTime]::UtcNow -lt $deadline)
 if($adapter.Status -ne 'Up'){throw 'Adapter did not return Up'}
 $current=Get-NetAdapterAdvancedProperty -Name $adapter.Name -RegistryKeyword '*InterruptModeration'
 if(($current.RegistryValue -join ',') -ne '0'){throw 'Temporary value did not apply'}
 Save-State 'temporary-applied.json' ([ordered]@{utc=[DateTime]::UtcNow.ToString('o');keyword=$current.RegistryKeyword;value=@($current.RegistryValue);status="$($adapter.Status)";link_speed="$($adapter.LinkSpeed)"})
 [IO.File]::WriteAllText((Join-Path $directory 'READY'),[DateTime]::UtcNow.ToString('o'),$utf8)
 $deadline=[DateTime]::UtcNow.AddSeconds(180)
 while(-not (Test-Path -LiteralPath (Join-Path $directory 'STOP')) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200}
} catch {Save-State 'FAILED.json' @{error=($_|Out-String);utc=[DateTime]::UtcNow.ToString('o')}}
finally {
 if($restoreNeeded){
  try {
   $adapter=Check-Adapter
   Set-NetAdapterAdvancedProperty -Name $adapter.Name -RegistryKeyword '*InterruptModeration' -RegistryValue $original -NoRestart
   Restart-NetAdapter -Name $adapter.Name -Confirm:$false
   $after=@(Get-NetAdapterAdvancedProperty -Name $adapter.Name|Select-Object RegistryKeyword,RegistryValue,DisplayName,DisplayValue)
   Save-State 'all-properties-after.json' $after
   foreach($p in $before){$a=@($after|Where-Object{$_.RegistryKeyword -eq $p.RegistryKeyword});if($a.Count -ne 1 -or ($a[0].RegistryValue -join ',') -ne ($p.RegistryValue -join ',')){throw "Property restoration mismatch: $($p.RegistryKeyword)"}}
   Save-State 'RESTORED.json' ([ordered]@{passed=$true;keyword='*InterruptModeration';restored=$original;all_advanced_properties_restored=$true;utc=[DateTime]::UtcNow.ToString('o')})
  } catch {Save-State 'RESTORE_FAILED.json' @{error=($_|Out-String);utc=[DateTime]::UtcNow.ToString('o')}}
 }
 Stop-Transcript -ErrorAction SilentlyContinue|Out-Null
}