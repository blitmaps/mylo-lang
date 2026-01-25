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

            // This launches:  ./mylo --debug /path/to/file.mylo
            return new vscode.DebugAdapterExecutable(
                myloPath,
                ['--debug', program]
            );
        }
    }));
}

function deactivate() {}

module.exports = { activate, deactivate };
