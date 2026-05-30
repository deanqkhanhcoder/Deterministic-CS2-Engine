$app = Start-Process -PassThru -FilePath 'runtime\bin\marco.exe'
Start-Sleep -Seconds 2
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.SendKeys]::SendWait('{F3}')
Start-Sleep -Seconds 2
Stop-Process -Id $app.Id
