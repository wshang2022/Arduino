# ---------- State ----------
$BaseDir = if ($env:APPDATA) { $env:APPDATA } else { "$env:USERPROFILE\AppData\Roaming" }
$StateFile = Join-Path $BaseDir 'arduino_com_ports.csv'

# ---------- Known Arduino VID/PID ----------
# Update this table in case COM detection fail to recognize
# Get-CimInstance Win32_PnPEntity | 
# Where-Object { $_.Name -match '\(COM\d+\)' -and $_.DeviceID -match 'USB.VID' } |
# % {Write-Host $_.DeviceID}
#
$KnownBoards = @{
    'VID_2341&PID_0043' = 'Arduino Uno'
    'VID_0403&PID_6001' = 'Arduino Nano (FTDI)'
    'VID_1A86&PID_7523' = 'Arduino / CH340'
    'VID_10C4&PID_EA60' = 'ESP32 / ESP8266'
    'VID_303A&PID_1001' = 'ESP32 / C3'
    'VID_2E8A&PID_0005' = 'RP2040 Pico'
}

# ---------- FAST: COM ports ----------
$reg = Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM' -ErrorAction SilentlyContinue
$coms = if ($reg) {
    $reg.PSObject.Properties |
        Where-Object { $_.MemberType -eq 'NoteProperty' -and $_.Value -match '^COM\d+$' } |
        ForEach-Object { $_.Value }
} else { @() }

# ---------- PnP lookup ----------
$pnp = Get-CimInstance Win32_PnPEntity |
       Where-Object { $_.Name -match '\(COM\d+\)' }

$current = foreach ($d in $pnp) {

    if ($d.DeviceID -notmatch 'VID_[0-9A-F]{4}&PID_[0-9A-F]{4}') { continue }
    $vidpid = $Matches[0]
    if (-not $KnownBoards.ContainsKey($vidpid)) { continue }

    if ($d.Name -match '\((COM\d+)\)') {
        $com = $Matches[1]
    } else { continue }

    [PSCustomObject]@{
        ComPort     = $com
        Board       = $KnownBoards[$vidpid]
        VidPid      = $vidpid
        Serial      = ($d.DeviceID -split '\\')[-1]
    }
}

$current = $current | Sort-Object ComPort

# ---------- Load previous ----------
$previous = if (Test-Path $StateFile) {
    Import-Csv $StateFile
} else { @() }
$previous = @($previous)
$current  = @($current)

# ---------- Diff ----------
#$diff = Compare-Object $previous $current -Property ComPort
$diff = Compare-Object @($previous) @($current) -Property ComPort

# ---------- Output ----------
Write-Host "Arduino Devices:" -ForegroundColor Cyan
foreach ($c in $current) {
    Write-Host ("  {0,-6} {1,-20} {2}" -f $c.ComPort, $c.Board, $c.Serial)
}

foreach ($d in $diff) {
    if ($d.SideIndicator -eq '=>') {
        Write-Host ("NEW        : {0,-6} {1}" -f $d.ComPort, $d.Board) -ForegroundColor Green
    } elseif ($d.SideIndicator -eq '<=') {
        Write-Host ("REMOVED    : {0,-6} {1}" -f $d.ComPort, $d.Board) -ForegroundColor Red
    }
}

# ---------- Save ----------
$current | Export-Csv $StateFile -NoTypeInformation

