# MCDeploy Windows installer

Run `build-installer.ps1` to produce one distributable Windows setup executable:

```powershell
powershell -ExecutionPolicy Bypass -File .\installer\build-installer.ps1
```

The script builds only the native `frontend/` dashboard and C++ application. The separately hosted public `WEBSITE/` webpanel is explicitly excluded.

## Fixed installation layout

- Application: `C:\Program Files\MCDeploy`
- Runtime configuration/cache: `%LOCALAPPDATA%\MCDeploy`
- Minecraft servers: `%USERPROFILE%\MCDeploy\Servers`
- Backups: `%USERPROFILE%\MCDeploy\Backups`

The path-selection page is disabled. The native MCDeploy WebView2 window is always used. Microsoft Visual C++ and WebView2 prerequisites are embedded in the setup EXE.

## Wizard options

- Add MCDeploy to the Start Menu
- Create a desktop shortcut
- Add a Start Menu shortcut to MCDeploy data
- Start MCDeploy at Windows sign-in
- Allow private-network dashboard access
- Launch MCDeploy after setup

## Output

`installer\output\MCDeploy-1.0.0-x64-Setup.exe`

Existing runtime configuration, servers, and backups are preserved during upgrades and uninstall. Never place API credentials or a populated database in installer staging.