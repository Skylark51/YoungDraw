param([ValidateSet('Prepare','Reopen')][string]$Stage='Prepare',[int]$ReuseTestProcessId=0)
$ErrorActionPreference='Stop'
$taskExisting=@(Get-Process POWERPNT -ErrorAction SilentlyContinue)
if($taskExisting.Count -and ($taskExisting.Count -ne 1 -or $taskExisting[0].Id -ne $ReuseTestProcessId)){throw 'PowerPoint is already running; leave user session untouched.'}
$taskOutput=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/powerpoint-background'))
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskCanQuit=$false
$taskApp=$null
$taskPresentation=$null
try {
    $taskApp=New-Object -ComObject PowerPoint.Application
    if($taskApp.Presentations.Count -or $taskApp.Windows.Count){throw 'PowerPoint has non-test documents; leave untouched.'}
    $taskCanQuit=$true
    $taskApp.DisplayAlerts=1
    $taskApp.AutomationSecurity=3
    if($Stage -eq 'Prepare'){
        $taskPresentation=$taskApp.Presentations.Add(0)
        foreach($taskCase in 0..5){
            $taskSlide=$taskPresentation.Slides.Add($taskCase+1,12)
            $taskImage=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../build/release-v04-final/office-storage-verification/$taskCase.emf"))
            $taskSlide.Shapes.AddPicture($taskImage,0,-1,20,20,650,450) | Out-Null
        }
        $taskTarget=Join-Path $taskOutput 'template.pptx'
    }else{
        $taskPresentation=$taskApp.Presentations.Open((Join-Path $taskOutput 'embedded.pptx'),0,0,0)
        $taskTarget=Join-Path $taskOutput 'reopened.pptx'
    }
    if($taskPresentation.Windows.Count -ne 0){throw 'Unexpected presentation window'}
    if($taskPresentation.Slides.Count -ne 6){throw 'Slide count changed'}
    $taskPresentation.SaveAs($taskTarget,24)
    Write-Output "PASS $Stage PowerPoint slides=6 windows=0 saved=$taskTarget"
} finally {
    if($null -ne $taskPresentation){$taskPresentation.Close()}
    if($null -ne $taskApp){if($taskCanQuit){$taskApp.Quit()};[Runtime.InteropServices.Marshal]::ReleaseComObject($taskApp) | Out-Null}
}
