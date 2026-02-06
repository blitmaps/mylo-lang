const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

function activate(context) {
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('mylo', {
        createDebugAdapterDescriptor: (session, executable) => {
            // ---------------------------------------------------------
            // CONFIGURATION: Point this to your compiled mylo executable
            // ---------------------------------------------------------
            // Tip: Use an absolute path for development
            const myloPath = '/path/to/cmake-build-debug/mylo';

            // We expect the launch.json to provide the "program"
            const program = session.configuration.program;

            if (!program) {
                vscode.window.showErrorMessage("No program specified in launch.json");
                return undefined;
            }

            // 1. Get CWD from config, or default to first workspace folder
            const cwd = session.configuration.cwd || (vscode.workspace.workspaceFolders ? vscode.workspace.workspaceFolders[0].uri.fsPath : undefined);

            const envConfig = session.configuration.env || {};
            const spawnedEnv = { ...process.env, ...envConfig };

            // 2. Pass 'cwd' in the options object (3rd argument)
            return new vscode.DebugAdapterExecutable(
                myloPath,
                ['--dap', program],
                {
                    env: spawnedEnv,
                    cwd: cwd
                }
            );
        }
    }));
}

function deactivate() {}

module.exports = { activate, deactivate };
