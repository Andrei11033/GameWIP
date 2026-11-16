'use strict';

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

// ------------------------------------------------------------
// Workspace and configuration
// ------------------------------------------------------------

function findRepositoryRoot() {
    for (const folder of vscode.workspace.workspaceFolders || []) {
        let directory = folder.uri.fsPath;

        while (true) {
            const targetCatalog = path.join(directory, 'scripts', 'config', 'build-targets.json');
            const projectConfig = path.join(directory, 'scripts', 'config', 'project.json');

            if (fs.existsSync(targetCatalog) && fs.existsSync(projectConfig)) {
                return directory;
            }

            const parent = path.dirname(directory);

            if (parent === directory) {
                break;
            }

            directory = parent;
        }
    }

    throw new Error('Open the GameWIP workspace to resolve its selected build target.');
}

function readJson(root, relativePath) {
    const fullPath = path.join(root, relativePath);

    try {
        return JSON.parse(fs.readFileSync(fullPath, 'utf8'));
    } catch (error) {
        throw new Error(`Could not read ${relativePath}: ${error.message}`);
    }
}

// ------------------------------------------------------------
// Selected target and build paths
// ------------------------------------------------------------

function getBuildContext() {
    const root = findRepositoryRoot();
    const project = readJson(root, path.join('scripts', 'config', 'project.json'));
    const catalog = readJson(root, path.join('scripts', 'config', 'build-targets.json'));
    const selectionPath = path.join(root, project.storage.state, 'build-target.json');
    let targetId;

    if (fs.existsSync(selectionPath)) {
        const selection = readJson(root, path.relative(root, selectionPath));

        if (selection.schemaVersion !== 1 || typeof selection.targetId !== 'string' || selection.targetId.trim() === '') {
            throw new Error(`Build target selection is malformed: ${selectionPath}`);
        }

        targetId = selection.targetId;
    } else if (process.platform === 'win32') {
        targetId = catalog.defaultTarget;
    } else {
        return { root, project, target: { id: 'native', platform: 'native' } };
    }

    const target = catalog.targets.find((entry) => entry.id.toLowerCase() === targetId.toLowerCase());

    if (target === undefined) {
        throw new Error(`Selected build target '${targetId}' is not in scripts/config/build-targets.json.`);
    }

    if (process.platform !== 'win32' && target.platform === 'windows') {
        throw new Error('The selected Windows build target is not supported by this host helper.');
    }

    return { root, project, target };
}

function getBuildDirectory(context, preset) {
    const targetDirectory = context.target.platform === 'native' ? [] : [context.target.id];
    return path.join(context.root, 'build', ...targetDirectory, preset);
}

// ------------------------------------------------------------
// VS Code launch values
// ------------------------------------------------------------

function getDevelopmentExecutable() {
    const context = getBuildContext();
    return path.join(getBuildDirectory(context, 'dev'), 'GameWIP.exe');
}

function getDevelopmentDirectory() {
    return getBuildDirectory(getBuildContext(), 'dev');
}

function getTargetRuntimePath() {
    const context = getBuildContext();

    if (context.target.platform === 'native') {
        return process.env.PATH || '';
    }

    const tools = readJson(context.root, path.join('scripts', 'config', 'project-tools.json'));
    const toolchain = tools.tools.find((entry) => entry.id === context.target.toolchain);

    if (toolchain === undefined || !toolchain.provider.managedDirectory || !toolchain.provider.binaryDirectory) {
        throw new Error(`Build target '${context.target.id}' has no managed runtime toolchain.`);
    }

    const toolchainRoot = path.join(
        context.project.managedEnvironment.gameWipToolsRoot,
        toolchain.provider.managedDirectory,
        toolchain.requiredVersion
    );
    const targetRuntime = path.join(toolchainRoot, context.target.compilerTriple, 'bin');
    const msys2Runtime = path.join(context.project.managedEnvironment.msys2Root, 'ucrt64', 'bin');

    return [
        path.join(toolchainRoot, toolchain.provider.binaryDirectory),
        targetRuntime,
        msys2Runtime,
        process.env.PATH || ''
    ].filter(Boolean).join(path.delimiter);
}

function getDebuggerPath() {
    const context = getBuildContext();

    if (process.platform !== 'win32') {
        throw new Error('The configured GDB path is available only on Windows.');
    }

    const debuggerPath = path.join(context.project.managedEnvironment.msys2Root, 'ucrt64', 'bin', 'gdb.exe');

    if (!fs.existsSync(debuggerPath)) {
        throw new Error(`GDB was not found at ${debuggerPath}. Run '.\\setup.bat repair' to install it.`);
    }

    return debuggerPath;
}

function activate(context) {
    const commands = [
        ['gamewip-workflows.developmentExecutable', getDevelopmentExecutable],
        ['gamewip-workflows.developmentDirectory', getDevelopmentDirectory],
        ['gamewip-workflows.targetRuntimePath', getTargetRuntimePath],
        ['gamewip-workflows.debuggerPath', getDebuggerPath]
    ];

    for (const [id, resolve] of commands) {
        context.subscriptions.push(vscode.commands.registerCommand(id, resolve));
    }
}

module.exports = { activate };
