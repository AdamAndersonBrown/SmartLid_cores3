# Listen-IMU-Archive.ps1
$port = 3333
$endpoint = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Parse("0.0.0.0"), $port)
$udpClient = New-Object System.Net.Sockets.UdpClient
$udpClient.Client.Bind($endpoint)

# 1. MASSIVE BUFFER: 1MB instead of 8KB to prevent Windows packet drops
$udpClient.Client.ReceiveBufferSize = 1048576

$outDir = ".\training_data"
if (!(Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

Write-Host "Listening universally for Core2 IMU telemetry on UDP port $port..." -ForegroundColor Green
Write-Host "Archiving batched vectors to $outDir..." -ForegroundColor Cyan
Write-Host "Press Ctrl+C to stop." -ForegroundColor Gray

$globalLastTs = 0
$expectedDeltaUs = 20000
$toleranceUs = 15000

try {
    while ($true) {
        if ($udpClient.Available -gt 0) {
            # WAKE SYNC: Wait 500ms for the ESP32 to finish its 380ms spaced transmission
            Start-Sleep -Milliseconds 500
            
            # PHASE 1: LIGHTNING CAPTURE (Clear the Windows UDP buffer instantly)
            $rawPackets = @()
            while ($udpClient.Available -gt 0) {
                $content = $udpClient.Receive([ref]$endpoint)
                $rawPackets += [System.Text.Encoding]::UTF8.GetString($content)
            }
            
            # PHASE 2: IN-MEMORY PROCESSING (Prevents I/O bottlenecks)
            $burstTotalLogs = 0
            $idleCount = 0; $rattleCount = 0; $openCount = 0
            $minTs = [long]::MaxValue
            $maxTs = [long]::MinValue
            
            $idleLines = New-Object System.Collections.Generic.List[string]
            $rattleLines = New-Object System.Collections.Generic.List[string]
            $openLines = New-Object System.Collections.Generic.List[string]

            foreach ($payload in $rawPackets) {
                try {
                    $jsonArray = $payload | ConvertFrom-Json
                    foreach ($record in $jsonArray) {
                        $tag = $record.tag
                        $ts = [long]$record.ts
                        
                        # Filter out corrupted 0-timestamps
                        if ($ts -gt 0) {
                            if ($ts -lt $minTs) { $minTs = $ts }
                            if ($ts -gt $maxTs) { $maxTs = $ts }
                        }
                        
                        # CONTINUITY WATCHDOG
                        if ($globalLastTs -gt 0) {
                            $delta = $ts - $globalLastTs
                            
                            # If the gap between logs exceeds 35ms, data was lost in transit
                            if ($delta -gt ($expectedDeltaUs + $toleranceUs)) {
                                $droppedFrames = [math]::Round($delta / $expectedDeltaUs) - 1
                                Write-Host "`n[!] NETWORK DROP: Time gap of ${delta}us detected! Roughly $droppedFrames frames lost in the ether." -ForegroundColor Red
                            }
                        }
                        $globalLastTs = $ts
                        
                        $recordString = $record | ConvertTo-Json -Compress
                        
                        if ($tag -eq 1) { 
                            $rattleCount++; $rattleLines.Add($recordString)
                        } elseif ($tag -eq 2) { 
                            $openCount++; $openLines.Add($recordString)
                        } else { 
                            $idleCount++; $idleLines.Add($recordString)
                        }
                        $burstTotalLogs++
                    }
                } catch { }
            }
            
            # PHASE 3: BULK DISK WRITE (One fast I/O operation per file)
            if ($idleLines.Count -gt 0) { Add-Content -Path (Join-Path $outDir "class_0_idle.jsonl") -Value $idleLines }
            if ($rattleLines.Count -gt 0) { Add-Content -Path (Join-Path $outDir "class_1_rattle.jsonl") -Value $rattleLines }
            if ($openLines.Count -gt 0) { Add-Content -Path (Join-Path $outDir "class_2_open.jsonl") -Value $openLines }
            
            $timeSpanSec = 0
            if ($minTs -ne [long]::MaxValue -and $maxTs -ne [long]::MinValue) {
                $timeSpanSec = [math]::Round(($maxTs - $minTs) / 1000000.0, 2)
            }
            
            Write-Host "Burst of $($rawPackets.Count) packets ($burstTotalLogs logs) | Span: ${timeSpanSec}s ->" -NoNewline
            if ($idleCount -gt 0) { Write-Host " [ IDLE: $idleCount ]" -ForegroundColor DarkGray -NoNewline }
            if ($rattleCount -gt 0) { Write-Host " [ RATTLE: $rattleCount ]" -ForegroundColor Yellow -NoNewline }
            if ($openCount -gt 0) { Write-Host " [ OPEN: $openCount ]" -ForegroundColor Magenta -NoNewline }
            Write-Host ""
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
} finally {
    $udpClient.Close()
    Write-Host "Socket closed."
}
