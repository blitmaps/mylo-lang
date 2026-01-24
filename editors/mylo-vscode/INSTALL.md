## Install the Extension
VS Code looks for extensions in a specific folder. The easiest way to install a development extension is to create a generic symbolic link (shortcut).

Navigate to your `~/.vscode/extensions/` folder (create it if it doesn't exist).

Linux/Mac: `cd ~/.vscode/extensions`

Windows: `cd %USERPROFILE%\.vscode\extensions`

### Link your folder:

Linux/Mac: `ln -s /path/to/mylo-vscode mylo-debug`

Windows: `mklink /D mylo-debug C:\path\to\mylo-vscode`

(Alternatively, you can just copy the whole mylo-vscode folder into the extensions directory).

Restart VS Code.