# SCRUM-362: fixed-duration capture from C29's Thingy:53 and rate/jitter/loss analysis.
# Usage: .\capture_c29.ps1 -Label stationary_60s -Seconds 60 -Port COM5
# Output: data\<Label>.csv (raw capture incl. header) and data\<Label>_metrics.txt
param(
    [string]$Label = "capture",
    [int]$Seconds = 60,
    [string]$Port = "COM5",
    [int]$TargetHz = 100
)

$inv = [Globalization.CultureInfo]::InvariantCulture
function F($x, $d) { return ([double]$x).ToString("F$d", $inv) }

$dir = Join-Path $PSScriptRoot "data"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$csv = Join-Path $dir "$Label.csv"
$metricsFile = Join-Path $dir "$Label`_metrics.txt"

# ---- capture ----
$p = New-Object System.IO.Ports.SerialPort $Port, 115200
$p.ReadTimeout = 500
$p.DtrEnable = $true     # opening the port with DTR asserted starts a fresh capture on the board
$p.Open()
$lines = New-Object System.Collections.Generic.List[string]
$sw = [Diagnostics.Stopwatch]::StartNew()
Write-Host "Capturing $Seconds s from $Port ..."
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    try { $lines.Add($p.ReadLine().TrimEnd("`r")) } catch [System.TimeoutException] { }
}
$p.Close()
$raw = Join-Path $dir "$Label`_raw.txt"
[IO.File]::WriteAllLines($raw, $lines)
Write-Host "Captured $($lines.Count) lines (raw copy: $raw)"

# ---- parse ----
$seqs = New-Object System.Collections.Generic.List[int64]
$ts = New-Object System.Collections.Generic.List[int64]
$header = New-Object System.Collections.Generic.List[string]
$clean = New-Object System.Collections.Generic.List[string]
$inData = $false
$logLines = 0
$stale = 0
$bad = 0
$stat = ""
foreach ($l in $lines) {
    if (-not $inData) {
        # Leftover rows of the previous capture can still be buffered in the USB port.
        # They may even be glued to the start of the new header line: drop them.
        $k = $l.IndexOf("# schema_version=")
        if ($k -gt 0) { $stale++; $l = $l.Substring($k) }
        elseif ($l -match '^\d+,') { $stale++; continue }
    }
    if ($l.StartsWith("#")) {
        if ($l.StartsWith("# STAT")) { $stat = $l } else { $header.Add($l) }
        $clean.Add($l)
        continue
    }
    if ($l.StartsWith("seq,")) { $inData = $true; $clean.Add($l); continue }
    if (-not $inData) {
        # Zephyr boot/IMU log lines printed before the CSV header: keep them as comments
        if ($l.Trim().Length -gt 0) { $clean.Add("# log: $l"); $logLines++ }
        continue
    }
    $f = $l.Split(",")
    $s = 0L; $t = 0L
    $ok = ($f.Count -eq 8) -and [int64]::TryParse($f[0], [ref]$s) -and [int64]::TryParse($f[1], [ref]$t)
    if ($ok) {
        for ($i = 2; $i -lt 8; $i++) {
            $d = 0.0
            if (-not [double]::TryParse($f[$i], [Globalization.NumberStyles]::Float, $inv, [ref]$d)) { $ok = $false }
        }
    }
    if ($ok) { $seqs.Add($s); $ts.Add($t); $clean.Add($l) } else { $bad++; $clean.Add("# BAD LINE: $l") }
}
[IO.File]::WriteAllLines($csv, $clean)
Write-Host "Saved $csv"

$n = $seqs.Count
if ($n -lt 2) { Write-Host "Not enough valid samples ($n)."; exit 1 }

# ---- metrics ----
$nonMono = 0
$dts = New-Object System.Collections.Generic.List[double]
for ($i = 1; $i -lt $n; $i++) {
    if ($ts[$i] -le $ts[$i - 1]) { $nonMono++ }
    if ($seqs[$i] -eq $seqs[$i - 1] + 1) { $dts.Add([double]($ts[$i] - $ts[$i - 1])) }
}
$durS = ($ts[$n - 1] - $ts[0]) / 1e6
$expected = $seqs[$n - 1] - $seqs[0] + 1
$lost = $expected - $n
$lossPct = 100.0 * $lost / $expected
$schedRate = ($expected - 1) / $durS
$effRate = ($n - 1) / $durS

$period = 1e6 / $TargetHz
$sum = 0.0; $min = [double]::MaxValue; $max = 0.0; $maxDev = 0.0
foreach ($d in $dts) {
    $sum += $d
    if ($d -lt $min) { $min = $d }
    if ($d -gt $max) { $max = $d }
    $dev = [math]::Abs($d - $period)
    if ($dev -gt $maxDev) { $maxDev = $dev }
}
$mean = $sum / $dts.Count
$var = 0.0
foreach ($d in $dts) { $var += ($d - $mean) * ($d - $mean) }
$std = [math]::Sqrt($var / $dts.Count)

$out = New-Object System.Collections.Generic.List[string]
$out.Add("SCRUM-362 capture metrics: $Label")
$out.Add("file                    : $csv")
$out.Add("header lines            : $($header.Count)" + $(if ($header.Count -eq 0) { "  (MISSING - port was not opened fresh)" } else { "" }))
$out.Add("stale lines discarded   : $stale  (buffered rows from the previous capture, before the header)")
$out.Add("boot log lines          : $logLines  (kept as '# log:' comments)")
$out.Add("target rate             : $TargetHz Hz (period $(F $period 0) us)")
$out.Add("valid samples (n)       : $n   (bad data lines: $bad)")
$out.Add("seq range               : $($seqs[0]) .. $($seqs[$n - 1])  (expected $expected)")
$out.Add("lost samples            : $lost   loss = $(F $lossPct 3) %")
$out.Add("duration (t_us)         : $(F $durS 3) s")
$out.Add("schedule rate           : $(F $schedRate 3) Hz")
$out.Add("effective sample rate   : $(F $effRate 3) Hz")
$out.Add("non-monotonic timestamps: $nonMono")
$out.Add("dt mean / min / max     : $(F $mean 1) / $(F $min 0) / $(F $max 0) us   (consecutive samples only, n=$($dts.Count))")
$out.Add("jitter (std of dt)      : $(F $std 1) us")
$out.Add("max |dt - period|       : $(F $maxDev 0) us")
$out.Add("device counters (last)  : $stat")
[IO.File]::WriteAllLines($metricsFile, $out)
$out | ForEach-Object { Write-Host $_ }
Write-Host "Saved $metricsFile"