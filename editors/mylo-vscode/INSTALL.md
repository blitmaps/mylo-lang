#### Install from Extension using VSIX Method (Recommended)

##### Install the packaging tool (requires Node.js/npm):

````Bash
sudo npm install -g vsce
````

##### Generating a package
Go to the extension source folder (mylo-vscode) and run:

````Bash
vsce package
````
(If it complains about a missing Package, README or LICENSE, just say continue 'Y')


##### Install it

Run this command (using the code command for official VS Code):

````Bash
code --install-extension mylo-debug-x.y.z.vsix
````
Verify: Restart VS Code and check the Extensions list (Ctrl+Shift+X). You should see it there.