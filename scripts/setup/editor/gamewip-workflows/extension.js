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
        throw new Error(`Could not read ${relativePath}: ${error.message}`, { cause: error });
    }
}

function getStorageRoot(root, project) {
    const configuredRoot = process.env.GAMEWIP_STORAGE_ROOT || project.storage.root;
    const storageRoot = path.resolve(root, configuredRoot);
    const buildRoot = path.resolve(root, 'build');
    const relativePath = path.relative(buildRoot, storageRoot);

    if (relativePath === '' || relativePath.startsWith(`..${path.sep}`) || relativePath === '..' || path.isAbsolute(relativePath)) {
        throw new Error(`GAMEWIP_STORAGE_ROOT must be a child of ${buildRoot}: ${storageRoot}`);
    }

    return storageRoot;
}

function getManagedRoot(context, setting, environmentVariable) {
    const configuredPath = process.env[environmentVariable] || context.project.managedEnvironment[setting];
    const managedRoot = path.resolve(context.storageRoot, configuredPath);

    if (managedRoot === path.parse(managedRoot).root || managedRoot === context.storageRoot) {
        throw new Error(`${environmentVariable} cannot point to a filesystem root or the storage root.`);
    }

    return managedRoot;
}

// ------------------------------------------------------------
// Selected target and build paths
// ------------------------------------------------------------

function getBuildContext() {
    const root = findRepositoryRoot();

    // Read the shared project and target configuration.
    const project = readJson(root, path.join('scripts', 'config', 'project.json'));
    const catalog = readJson(root, path.join('scripts', 'config', 'build-targets.json'));
    const storageRoot = getStorageRoot(root, project);
    const selectionPath = path.join(storageRoot, project.storage.state, 'build-target.json');
    let targetId;

    // Use the persisted selection or the configured default.
    if (fs.existsSync(selectionPath)) {
        const selection = readJson(root, path.relative(root, selectionPath));

        if (selection.schemaVersion !== 1 || typeof selection.targetId !== 'string' || selection.targetId.trim() === '') {
            throw new Error(`Build target selection is malformed: ${selectionPath}`);
        }

        targetId = selection.targetId;
    } else {
        targetId = catalog.defaultTarget;
    }

    // Refuse launch configurations that cannot run on this host.
    const target = catalog.targets.find((entry) => entry.id.toLowerCase() === targetId.toLowerCase());

    if (target === undefined) {
        throw new Error(`Selected build target '${targetId}' is not in scripts/config/build-targets.json.`);
    }

    if (process.platform !== 'win32' && target.platform === 'windows') {
        throw new Error('VS Code cannot launch or debug the selected Windows target on this host. Build it here, then run or debug it on Windows.');
    }

    return { root, project, storageRoot, target };
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

    // Native executables use the current host environment.
    if (context.target.platform === 'native') {
        return process.env.PATH || '';
    }

    const tools = readJson(context.root, path.join('scripts', 'config', 'project-tools.json'));
    const toolchain = tools.tools.find((entry) => entry.id === context.target.toolchain);

    if (toolchain === undefined || !toolchain.provider.managedDirectory || !toolchain.provider.binaryDirectory) {
        throw new Error(`Build target '${context.target.id}' has no managed runtime toolchain.`);
    }

    const toolchainRoot = path.join(
        getManagedRoot(context, 'gameWipToolsRoot', 'GAMEWIP_MANAGED_TOOLS_ROOT'),
        toolchain.provider.managedDirectory,
        toolchain.requiredVersion,
    );
    const targetRuntime = path.join(toolchainRoot, context.target.compilerTriple, 'bin');
    const msys2Runtime = path.join(getManagedRoot(context, 'msys2Root', 'GAMEWIP_MSYS2_ROOT'), 'ucrt64', 'bin');

    // Put the selected toolchain and runtime directories ahead of the host PATH.
    return [path.join(toolchainRoot, toolchain.provider.binaryDirectory), targetRuntime, msys2Runtime, process.env.PATH || '']
        .filter(Boolean)
        .join(path.delimiter);
}

function getDebuggerPath() {
    const context = getBuildContext();

    if (process.platform !== 'win32') {
        throw new Error('The configured GDB path is available only on Windows.');
    }

    const debuggerPath = path.join(getManagedRoot(context, 'msys2Root', 'GAMEWIP_MSYS2_ROOT'), 'ucrt64', 'bin', 'gdb.exe');

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
        ['gamewip-workflows.debuggerPath', getDebuggerPath],
    ];

    for (const [id, resolve] of commands) {
        context.subscriptions.push(vscode.commands.registerCommand(id, resolve));
    }
}

module.exports = { activate };
