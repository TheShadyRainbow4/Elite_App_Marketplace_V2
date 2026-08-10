param(
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$DroppedFiles
)

Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName PresentationCore

$global:LogDir = "$env:SystemDrive\EliteSoftware\Logs"
if (-not (Test-Path $global:LogDir)) { New-Item -Path $global:LogDir -ItemType Directory -Force | Out-Null }
$global:LogFile = Join-Path $global:LogDir "IcoToPngConverter.log"

function Write-Log {
    param([string]$Message, [string]$Type="INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logEntry = "[$timestamp] [$Type] $Message"
    Add-Content -Path $global:LogFile -Value $logEntry
}

function Convert-IcoToPng {
    param([string]$IcoPath)
    try {
        Write-Log "Processing: $IcoPath"
        $baseDir = Split-Path $IcoPath -Parent
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($IcoPath)
        
        # Standardized folder name for all converted PNGs in this directory
        $outDir = Join-Path $baseDir "Converted_PNGs"
        if (-not (Test-Path $outDir)) {
            New-Item -Path $outDir -ItemType Directory -Force | Out-Null
        }
        
        $stream = [System.IO.File]::OpenRead($IcoPath)
        try {
            $decoder = New-Object System.Windows.Media.Imaging.IconBitmapDecoder($stream, 'None', 'Default')
            $count = 0
            foreach ($frame in $decoder.Frames) {
                $width = $frame.PixelWidth
                $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
                $encoder.Frames.Add($frame)
                
                $outPath = Join-Path $outDir "${baseName}x${width}.png"
                $outStream = [System.IO.File]::Create($outPath)
                try {
                    $encoder.Save($outStream)
                    Write-Log "Created: $outPath"
                } finally {
                    $outStream.Dispose()
                }
                $count++
            }
            Write-Log "Successfully extracted $count images from $IcoPath"
        } finally {
            $stream.Dispose()
        }
    } catch {
        Write-Log "Error processing $IcoPath : $_" "ERROR"
    }
}

if ($DroppedFiles) {
    foreach ($file in $DroppedFiles) {
        if (Test-Path $file) {
            if ($file.EndsWith(".ico", [System.StringComparison]::OrdinalIgnoreCase)) {
                Convert-IcoToPng -IcoPath $file
            }
        }
    }
}
