# Renders pages of a PDF to PNG (and optionally OCR text beside each) with Windows' own PDF and OCR engines, for the
# Study's course ingestion (study/INGEST.md: "every page is rendered as an image and looked at").
#   tools\pdfpages.ps1 -Pdf <file> [-From 1] [-To 5] [-Out dir] [-Width 1100] [-Ocr] [-Count]
#   -Count only prints the page count.
param([Parameter(Mandatory = $true)][string]$Pdf, [int]$From = 1, [int]$To = 0, [string]$Out = "", [int]$Width = 1100, [switch]$Ocr, [switch]$Count)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskOp = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
$asTaskAct = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0]
function Await($op, [Type]$t) { $task = $asTaskOp.MakeGenericMethod($t).Invoke($null, @($op)); $task.Wait(-1) | Out-Null; $task.Result }
function AwaitAct($act) { $task = $asTaskAct.Invoke($null, @($act)); $task.Wait(-1) | Out-Null }
[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.StorageFolder, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null

$path = (Resolve-Path $Pdf).Path
$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($path)) ([Windows.Storage.StorageFile])
$doc = Await ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
if ($Count) { $doc.PageCount; return }
if ($To -le 0 -or $To -gt $doc.PageCount) { $To = [int]$doc.PageCount }
if ($Out -eq "") { $Out = Join-Path (Split-Path $path) ("pages_" + [IO.Path]::GetFileNameWithoutExtension($path)) }
New-Item -ItemType Directory -Force $Out | Out-Null
$folder = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync((Resolve-Path $Out).Path)) ([Windows.Storage.StorageFolder])
$engine = if ($Ocr) { [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() } else { $null }
for ($i = $From; $i -le $To; $i++) {
    $page = $doc.GetPage([uint32]($i - 1))
    $name = "p{0:D4}.png" -f $i
    $outFile = Await ($folder.CreateFileAsync($name, [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
    $stream = Await ($outFile.OpenAsync([Windows.Storage.FileAccessMode]::ReadWrite)) ([Windows.Storage.Streams.IRandomAccessStream])
    $opts = New-Object Windows.Data.Pdf.PdfPageRenderOptions
    $opts.DestinationWidth = [uint32]$Width
    AwaitAct ($page.RenderToStreamAsync($stream, $opts))
    if ($engine) {
        $stream.Seek(0)
        $dec = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
        $bmp = Await ($dec.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
        $res = Await ($engine.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])
        $lines = ($res.Lines | ForEach-Object { $_.Text }) -join "`n"
        [IO.File]::WriteAllText((Join-Path $Out ("p{0:D4}.txt" -f $i)), $lines)
        $bmp.Dispose()
    }
    $stream.Dispose(); $page.Dispose()
}
"rendered pages $From-$To of $($doc.PageCount) to $Out"
