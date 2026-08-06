$env:ELECTRON_RUN_AS_NODE = $null
Remove-Item Env:\ELECTRON_RUN_AS_NODE -ErrorAction SilentlyContinue
Set-Location D:\POC\GeoTerrain\OpenGeoStudio
& .\node_modules\electron\dist\electron.exe . --remote-debugging-port=9223 2>&1
