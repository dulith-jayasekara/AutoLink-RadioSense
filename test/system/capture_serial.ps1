param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [ValidateRange(1, 4000000)]
    [int]$BaudRate = 115200,

    [ValidatePattern('^[a-zA-Z0-9_-]+$')]
    [string]$TestName = "serial"
)

$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$logsDirectory = Join-Path $projectRoot "logs"
New-Item -ItemType Directory -Path $logsDirectory -Force | Out-Null

$serialPort = New-Object System.IO.Ports.SerialPort(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serialPort.Encoding = [System.Text.Encoding]::UTF8

$writer = $null
$pendingText = ""

try
{
    $serialPort.Open()

    $fileTimestamp = Get-Date -Format "yyyyMMdd_HHmmss_fff"
    $logFilePath = Join-Path $logsDirectory "${TestName}_${fileTimestamp}.log"
    $writer = [System.IO.StreamWriter]::new(
        $logFilePath,
        $false,
        [System.Text.UTF8Encoding]::new($false)
    )
    $writer.AutoFlush = $true

    Write-Host "Capturing $Port at $BaudRate baud."
    Write-Host "Log file: $logFilePath"
    Write-Host "Stop capture with Ctrl+C."

    while ($true)
    {
        $pendingText += $serialPort.ReadExisting()

        $newlineIndex = $pendingText.IndexOf("`n")
        while ($newlineIndex -ge 0)
        {
            $line = $pendingText.Substring(0, $newlineIndex)
            if ($line.EndsWith("`r"))
            {
                $line = $line.Substring(0, $line.Length - 1)
            }

            $pendingText = $pendingText.Substring($newlineIndex + 1)
            $entry = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-ddTHH:mm:ss.fffK"), $line
            $writer.WriteLine($entry)
            Write-Host $entry

            $newlineIndex = $pendingText.IndexOf("`n")
        }

        Start-Sleep -Milliseconds 25
    }
}
finally
{
    if ($null -ne $writer)
    {
        if ($pendingText.Length -gt 0)
        {
            $entry = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-ddTHH:mm:ss.fffK"), $pendingText
            $writer.WriteLine($entry)
        }

        $writer.Dispose()
    }

    if ($serialPort.IsOpen)
    {
        $serialPort.Close()
    }

    $serialPort.Dispose()
}
