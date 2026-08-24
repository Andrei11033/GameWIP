// Repository-owned pull-request metadata policy executed from trusted base-branch content.

'use strict';

const TITLE_PATTERN = /^[a-z][a-z0-9_-]*: [a-z0-9][^\r\n]*$/;
const REQUIRED_SECTIONS = ['Summary', 'Linked Issues', 'Validation', 'Merge Message', 'Checklist'];

function sectionText(body, name) {
    const match = new RegExp(`^##\\s+${name}\\s*$`, 'im').exec(body);
    if (!match) {
        return undefined;
    }
    const rest = body.slice(match.index + match[0].length);
    const next = /^##\s+/m.exec(rest);
    return (next ? rest.slice(0, next.index) : rest).trim();
}

function hasMeaningfulContent(text) {
    return Boolean(
        text
            ?.replace(/<!--[\s\S]*?-->/g, '')
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
    for (const prefix of ['area:', 'type:', 'priority:']) {
        if (!labels.some((label) => label.startsWith(prefix))) {
            errors.push(`PR must have a \`${prefix}*\` label.`);
        }
    }
    return errors;
}

module.exports = { hasMeaningfulContent, sectionText, validatePullRequest };
