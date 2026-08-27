// Repository-owned pull-request metadata policy executed from trusted base-branch content.

'use strict';

const TITLE_PATTERN = /^[a-z][a-z0-9_-]*: [a-z0-9][^\r\n]*$/;
const REQUIRED_SECTIONS = ['Summary', 'Linked Issues', 'Validation', 'Merge Message', 'Checklist'];
const ALLOWED_TYPE_LABELS = new Set(['type:bug', 'type:feature', 'type:task', 'type:decision', 'type:release']);
const ALLOWED_PRIORITY_LABELS = new Set(['priority:high', 'priority:normal', 'priority:low']);
const ALLOWED_COMPATIBILITY_LABELS = new Set(['compat:breaking']);

function sectionText(body, name) {
    const match = new RegExp(`^##\\s+${name}\\s*$`, 'im').exec(body);
    if (!match) {
        return undefined;
    }
    const rest = body.slice(match.index + match[0].length);
    const next = /^##\s+/m.exec(rest);
    return (next ? rest.slice(0, next.index) : rest).trim();
}

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

function hasMeaningfulContent(text) {
    return Boolean(
        text &&
        stripHtmlComments(text)
            .replace(/^-+\s*$/gm, '')
            .trim(),
    );
}

function validatePullRequest(pr) {
    const errors = [];
    if (!TITLE_PATTERN.test(pr.title)) {
        errors.push('PR title must use `area: imperative summary`.');
    }
    if (/\s\(#\d+\)$/.test(pr.title)) {
        errors.push("PR title should not include GitHub's `(#number)` suffix.");
    }
    const body = pr.body || '';
    for (const name of REQUIRED_SECTIONS) {
        if (sectionText(body, name) === undefined) {
            errors.push(`PR body must include a \`## ${name}\` section.`);
        }
    }
    for (const name of ['Summary', 'Validation']) {
        const value = sectionText(body, name);
        if (value !== undefined && !hasMeaningfulContent(value)) {
            errors.push(`\`## ${name}\` must contain meaningful content.`);
        }
    }
    const linked = sectionText(body, 'Linked Issues');
    if (linked !== undefined && !/\b(close[sd]?|fix(e[sd])?|resolve[sd]?)\s+#\d+\b/i.test(linked) && !/\bno linked issue\s*:\s*\S/i.test(linked)) {
        errors.push('`## Linked Issues` must contain `Closes #123` or `No linked issue: reason`.');
    }
    const merge = sectionText(body, 'Merge Message');
    if (merge !== undefined) {
        const title = /^(?:-\s*)?Title:\s*`?([^`\r\n]+)`?\s*$/im.exec(merge)?.[1]?.trim();
        if (!title || title === 'area: imperative summary' || !TITLE_PATTERN.test(title)) {
            errors.push('Merge message title must be concrete and use `area: imperative summary`.');
        }
        const mergeBody = /^(?:-\s*)?Body:\s*(.+)$/im.exec(merge)?.[1];
        if (!hasMeaningfulContent(mergeBody)) {
            errors.push('Merge message must include a non-empty `Body: explanation` line.');
        }
    }
    const labels = (pr.labels || []).map((label) => label.name);
    const areaLabels = labels.filter((label) => label.startsWith('area:'));
    const typeLabels = labels.filter((label) => label.startsWith('type:'));
    const priorityLabels = labels.filter((label) => label.startsWith('priority:'));
    if (areaLabels.length !== 1) {
        errors.push(`PR must have exactly one \`area:*\` label; found ${areaLabels.length}.`);
    } else if (!/^area:[a-z0-9]+(?:-[a-z0-9]+)*$/.test(areaLabels[0])) {
        errors.push(`PR area label is not canonical: \`${areaLabels[0]}\`.`);
    }
    if (typeLabels.length !== 1) {
        errors.push(`PR must have exactly one \`type:*\` label; found ${typeLabels.length}.`);
    } else if (!ALLOWED_TYPE_LABELS.has(typeLabels[0])) {
        errors.push(`PR type label is not supported: \`${typeLabels[0]}\`.`);
    }
    if (priorityLabels.length !== 1) {
        errors.push(`PR must have exactly one \`priority:*\` label; found ${priorityLabels.length}.`);
    } else if (!ALLOWED_PRIORITY_LABELS.has(priorityLabels[0])) {
        errors.push(`PR priority label is not supported: \`${priorityLabels[0]}\`.`);
    }
    for (const label of labels.filter((candidate) => candidate.startsWith('compat:'))) {
        if (!ALLOWED_COMPATIBILITY_LABELS.has(label)) {
            errors.push(`PR compatibility label is not supported: \`${label}\`.`);
        }
    }
    return errors;
}

module.exports = { ALLOWED_PRIORITY_LABELS, ALLOWED_TYPE_LABELS, hasMeaningfulContent, sectionText, validatePullRequest };
