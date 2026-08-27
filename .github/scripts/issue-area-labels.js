// Repository-owned issue-area label policy executed by the trusted GitHub workflow.

'use strict';

const AREA_LABELS_BY_VALUE = new Map([
    ['CMake / build system', 'area:cmake'],
    ['FileSystem', 'area:filesystem'],
    ['IO', 'area:io'],
    ['Terminal', 'area:terminal'],
    ['Unicode', 'area:unicode'],
    ['Foundation shared systems', 'area:foundation'],
    ['Logger', 'area:logger'],
    ['Assert', 'area:assert'],
    ['TestSupport', 'area:test-support'],
    ['Input', 'area:input'],
    ['Action', 'area:action'],
    ['Window', 'area:window'],
    ['Window manager', 'area:window-manager'],
    ['Engine shared systems', 'area:engine'],
    ['Windows platform backend', 'area:platform-win32'],
    ['Math', 'area:math'],
    ['Rendering', 'area:rendering'],
    ['Simulation', 'area:simulation'],
    ['Audio', 'area:audio'],
    ['Tools / developer support', 'area:tools'],
    ['Documentation', 'area:docs'],
    ['GitHub / repository setup', 'area:github'],
    ['Roadmap / planning', 'area:roadmap'],
    ['Gameplay', 'area:gameplay'],
]);

const HISTORICAL_AREA_LABELS_BY_VALUE = new Map([
    ['Build and tooling', 'area:cmake'],
    ['Tools/developer support', 'area:tools'],
    ['GitHub/repository setup', 'area:github'],
    ['Gameplay roadmap', 'area:roadmap'],
]);

function stripHtmlComments(text) {
    let result = text;
    while (true) {
        const start = result.indexOf('<!--');
        if (start < 0) {
            return result;
        }

        const end = result.indexOf('-->', start + 4);
        result = end < 0 ? result.slice(0, start) : result.slice(0, start) + result.slice(end + 3);
    }
}

function readIssueFormField(body, heading) {
    const lines = body.split(/\r?\n/);
    const start = lines.findIndex((line) => line.trim() === `### ${heading}`);
    if (start < 0) {
        return undefined;
    }
    const values = [];
    for (const line of lines.slice(start + 1)) {
        if (line.startsWith('### ')) {
            break;
        }
        values.push(line);
    }
    return stripHtmlComments(values.join('\n')).trim();
}

function targetAreaLabel(body, currentLabels) {
    const selected = readIssueFormField(body, 'Area');
    if (selected === 'Engine input/action/window') {
        return ['area:input', 'area:action', 'area:window', 'area:window-manager'].find((label) => currentLabels.includes(label)) || 'area:engine';
    }
    return AREA_LABELS_BY_VALUE.get(selected) ?? HISTORICAL_AREA_LABELS_BY_VALUE.get(selected);
}

async function applyIssueAreaLabel({ github, context }) {
    const issue = context.payload.issue;
    const current = issue.labels.map((label) => label.name);
    const target = targetAreaLabel(issue.body ?? '', current);
    if (target === undefined) {
        return;
    }
    const request = { owner: context.repo.owner, repo: context.repo.repo, issue_number: context.issue.number };
    for (const label of new Set(AREA_LABELS_BY_VALUE.values())) {
        if (label !== target && current.includes(label)) {
            await github.rest.issues.removeLabel({ ...request, name: label });
        }
    }
    if (target && !current.includes(target)) {
        await github.rest.issues.addLabels({ ...request, labels: [target] });
    }
}

module.exports = { AREA_LABELS_BY_VALUE, HISTORICAL_AREA_LABELS_BY_VALUE, applyIssueAreaLabel, readIssueFormField, targetAreaLabel };
