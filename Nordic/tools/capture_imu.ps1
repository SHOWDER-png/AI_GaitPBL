param(
  [string]$Label = "stationary",
  [int]$Seconds = 10,
  [string]$Port = "COM5"
)
function ToNum($s) {
  $a = $s.Split('.')
  [double]$a[0] + [double]$a[1] / 1e6
}
$dir = "C:\imu_test\data"
New-Item -ItemType Directory -Force $dir | Out-Null
$out = "$dir\$Label.csv"
"t_ms,ax_mps2,ay_mps2,az_mps2,gx_rads,gy_rads,gz_rads" | Out-File $out -Encoding ascii
$p = New-Object System.IO.Ports.SerialPort $Port,115200
$p.DtrEnable = $true
$p.ReadTimeout = 3000
$p.Open()
$sw = [Diagnostics.Stopwatch]::StartNew()
$n = 0
try {
  while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    $line = $p.ReadLine()
    if ($line -match 'AX:\s*(\S+);\s*AY:\s*(\S+);\s*AZ:\s*(\S+);\s*GX:\s*(\S+);\s*GY:\s*(\S+);\s*GZ:\s*(\S+);') {
      $v = 1..6 | ForEach-Object { ToNum $Matches[$_] }
      ("{0},{1}" -f $sw.ElapsedMilliseconds, ($v -join ",")) | Out-File $out -Append -Encoding ascii
      $n++
    }
  }
} finally { $p.Close() }
"$n samples saved to $out"