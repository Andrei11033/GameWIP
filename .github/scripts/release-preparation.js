'use strict';

function compareReleaseVersions(left, right) {
    for (const field of ['major', 'minor', 'patch']) {
        if (left[field] < right[field]) {
            return -1;
        }

        if (left[field] > right[field]) {
            return 1;
        }
    }

    return 0;
}

function parseReleaseVersion(value) {
    const match = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.exec(value);
    if (match === null) {
        throw new Error(`Invalid release version: ${value}`);
    }

    return {
        text: value,
        major: Number.parseInt(match[1], 10),
        minor: Number.parseInt(match[2], 10),
        patch: Number.parseInt(match[3], 10),
    };
}

function parseProjectVersion(cmakeContents) {
    if (typeof cmakeContents !== 'string') {
        throw new Error('CMake contents must be a string.');
    }

    const uncommented = cmakeContents.replace(/#[^\r\n]*/g, '');
    const matches = [...uncommented.matchAll(/\bproject\s*\(\s*GameWIP\s+VERSION\s+([^\s)]+)/gi)];
    if (matches.length !== 1) {
        throw new Error(`Expected exactly one GameWIP project version, found ${matches.length}.`);
    }

    return parseReleaseVersion(matches[0][1]);
}

function parseReleaseTag(tagName) {
    if (typeof tagName !== 'string' || !tagName.startsWith('v')) {
        throw new Error(`Invalid release tag: ${tagName}`);
    }
    return parseReleaseVersion(tagName.slice(1));
}

function findLatestReleaseVersion(tagNames) {
    if (!Array.isArray(tagNames)) {
        throw new Error('Tag names must be an array.');
    }

    let latest = null;
    for (const tagName of tagNames) {
        if (typeof tagName !== 'string') {
            throw new Error('Every tag name must be a string.');
        }
        if (!tagName.startsWith('v')) {
            continue;
        }

        const version = parseReleaseTag(tagName);
        if (latest === null || compareReleaseVersions(version, latest) > 0) {
            latest = version;
        }
    }
    return latest;
}

function parseMilestoneReleaseMetadata(description) {
    if (typeof description !== 'string') {
        throw new Error('Milestone description must be a string.');
    }

    const lines = description.replace(/\r\n?/g, '\n').split('\n');
    const versionLines = lines.filter((line) => line.startsWith('Release version:'));
    const issueLines = lines.filter((line) => line.startsWith('Release issue:'));

    if (versionLines.length !== 1) {
        throw new Error(`Expected exactly one Release version line, found ${versionLines.length}.`);
    }

    if (issueLines.length > 1) {
        throw new Error(`Expected at most one Release issue line, found ${issueLines.length}.`);
    }

    const versionMatch = /^Release version: `([^`]+)`$/.exec(versionLines[0]);
    if (versionMatch === null) {
        throw new Error(`Invalid Release version line: ${versionLines[0]}`);
    }

    let releaseIssue = null;

    if (issueLines.length === 1) {
        const issueMatch = /^Release issue: `#([1-9]\d*)`$/.exec(issueLines[0]);
        if (issueMatch === null) {
            throw new Error(`Invalid Release issue line: ${issueLines[0]}`);
        }

        releaseIssue = Number.parseInt(issueMatch[1], 10);
    }

    return {
        version: parseReleaseVersion(versionMatch[1]),
        releaseIssue,
    };
}

function validateTargetVersion({targetVersion, projectVersion, latestVersion}) {
    if (compareReleaseVersions(targetVersion, projectVersion) !== 0) {
        throw new Error(
            `Milestone target ${targetVersion.text} does not match root project version ${projectVersion.text}.`,
        );
    }
    if (latestVersion !== null && compareReleaseVersions(targetVersion, latestVersion) <= 0) {
        throw new Error(
            `Milestone target ${targetVersion.text} must be newer than latest release ${latestVersion.text}.`,
        );
    }
    return true;
}

function validateMilestoneReadiness({activeMilestone, milestone, issues}) {
    if (!milestone || typeof milestone !== 'object') {
        throw new Error('A milestone is required.');
    }
    if (milestone.title !== activeMilestone) {
        throw new Error(`Selected milestone '${milestone.title}' is not active milestone '${activeMilestone}'.`);
    }
    if (String(milestone.state).toLowerCase() !== 'open') {
        throw new Error(`Milestone '${milestone.title}' must be open.`);
    }
    if (!Array.isArray(issues)) {
        throw new Error('Milestone issues must be an array.');
    }

    const metadata = parseMilestoneReleaseMetadata(milestone.description);
    if (metadata.releaseIssue === null) {
        throw new Error(`Milestone '${milestone.title}' does not designate a release issue.`);
    }

    const releaseIssues = issues.filter((issue) => issue.number === metadata.releaseIssue);
    if (releaseIssues.length !== 1) {
        throw new Error(
            `Milestone '${milestone.title}' must contain release issue #${metadata.releaseIssue} exactly once.`,
        );
    }
    if (String(releaseIssues[0].state).toLowerCase() !== 'open') {
        throw new Error(`Release issue #${metadata.releaseIssue} must remain open during preparation.`);
    }

    const otherOpenIssues = issues
        .filter((issue) => issue.number !== metadata.releaseIssue && String(issue.state).toLowerCase() === 'open')
        .map((issue) => issue.number)
        .sort((left, right) => left - right);
    if (otherOpenIssues.length > 0) {
        throw new Error(`Milestone '${milestone.title}' still has open implementation issues: ${otherOpenIssues.join(', ')}.`);
    }

    return {
        version: metadata.version,
        releaseIssue: releaseIssues[0],
    };
}

function validateRequiredChecks({masterSha, evaluatedSha, requiredChecks, checks}) {
    if (masterSha !== evaluatedSha) {
        throw new Error(`Evaluated commit ${evaluatedSha} is not latest master commit ${masterSha}.`);
    }
    if (!Array.isArray(requiredChecks) || requiredChecks.length === 0) {
        throw new Error('At least one required validation check must be configured.');
    }
    if (!checks || typeof checks !== 'object' || Array.isArray(checks)) {
        throw new Error('Validation checks must be keyed by required check name.');
    }

    for (const name of requiredChecks) {
        const check = checks[name];
        if (!check) {
            throw new Error(`Required validation check is missing: ${name}.`);
        }
        if (check.headSha !== masterSha) {
            throw new Error(`Required validation check '${name}' belongs to ${check.headSha}, not ${masterSha}.`);
        }
        if (String(check.status).toLowerCase() !== 'completed' || String(check.conclusion).toLowerCase() !== 'success') {
            throw new Error(`Required validation check '${name}' has not completed successfully.`);
        }
    }
    return true;
}

function roadmapMilestoneNumber(title) {
    const match = /^R(\d{2}) - /.exec(title);
    if (match === null) {
        throw new Error(`Invalid roadmap milestone title: ${title}`);
    }
    return Number.parseInt(match[1], 10);
}

function selectNextMilestone(milestones, currentTitle) {
    if (!Array.isArray(milestones)) {
        throw new Error('Milestones must be an array.');
    }

    const currentNumber = roadmapMilestoneNumber(currentTitle);
    if (currentNumber === 52) {
        return null;
    }

    const expectedNumber = currentNumber + 1;
    const matches = milestones.filter((milestone) => {
        const match = /^R(\d{2}) - /.exec(milestone.title);
        return match !== null && Number.parseInt(match[1], 10) === expectedNumber;
    });
    if (matches.length !== 1) {
        throw new Error(
            `Expected exactly one R${String(expectedNumber).padStart(2, '0')} milestone, found ${matches.length}.`,
        );
    }
    return matches[0];
}

function releaseNames(version) {
    const tagName = `v${version.text}`;
    return {
        tagName,
        branchName: `release/${tagName}`,
        pullRequestTitle: `release: prepare ${tagName}`,
        releaseName: `GameWIP ${tagName}`,
    };
}

function planPreparationArtifacts({version, masterSha, branches = [], pullRequests = []}) {
    const names = releaseNames(version);
    const matchingBranches = branches.filter((branch) => branch.name === names.branchName);
    if (matchingBranches.length > 1) {
        throw new Error(`Multiple release branches found for ${names.branchName}.`);
    }
    const branch = matchingBranches[0] ?? null;

    const matchingPullRequests = pullRequests.filter((pullRequest) => pullRequest.headRefName === names.branchName);
    if (matchingPullRequests.length > 1) {
        throw new Error(`Multiple release pull requests found for ${names.branchName}.`);
    }
    const pullRequest = matchingPullRequests[0] ?? null;
    if (pullRequest !== null && pullRequest.baseRefName !== 'master') {
        throw new Error(`Release pull request for ${names.branchName} does not target master.`);
    }

    const pullRequestState = pullRequest === null ? null : String(pullRequest.state).toLowerCase();
    if (pullRequestState === 'closed') {
        throw new Error(`Release pull request for ${names.branchName} was closed without merging.`);
    }
    if (pullRequestState !== null && !['open', 'merged'].includes(pullRequestState)) {
        throw new Error(`Unknown release pull request state: ${pullRequest.state}.`);
    }
    if (pullRequestState === 'open' && branch === null) {
        throw new Error(`Open release pull request for ${names.branchName} has no matching branch.`);
    }
    if (pullRequestState === null && branch !== null && branch.sha !== masterSha) {
        throw new Error(`Unclaimed release branch ${names.branchName} points to ${branch.sha}, not ${masterSha}.`);
    }
    if (pullRequestState === 'open') {
        if (pullRequest.headSha !== branch.sha) {
            throw new Error(`Release pull request head does not match branch ${names.branchName}.`);
        }
        if (pullRequest.mergeBaseSha !== masterSha) {
            throw new Error(`Release pull request is not based on latest master commit ${masterSha}.`);
        }
    }

    return {
        ...names,
        createBranch: branch === null && pullRequestState !== 'merged',
        createPullRequest: pullRequest === null,
        reuseBranch: branch !== null,
        reusePullRequest: pullRequest !== null,
        pullRequestMerged: pullRequestState === 'merged',
    };
}

function planFinalizationArtifacts({version, releaseCommitSha, tags = [], releases = []}) {
    const names = releaseNames(version);
    const matchingTags = tags.filter((tag) => tag.name === names.tagName);
    if (matchingTags.length > 1) {
        throw new Error(`Multiple tags found for ${names.tagName}.`);
    }
    const tag = matchingTags[0] ?? null;
    if (tag !== null && (!tag.annotated || tag.targetSha !== releaseCommitSha)) {
        throw new Error(`Existing tag ${names.tagName} is not the expected immutable annotated release tag.`);
    }

    const matchingReleases = releases.filter((release) => release.tagName === names.tagName);
    if (matchingReleases.length > 1) {
        throw new Error(`Multiple GitHub releases found for ${names.tagName}.`);
    }
    const release = matchingReleases[0] ?? null;
    if (release !== null && tag === null) {
        throw new Error(`GitHub release ${names.tagName} exists without its immutable tag.`);
    }

    return {
        ...names,
        createTag: tag === null,
        createRelease: release === null,
        publishDraftRelease: release?.draft === true,
        reuseTag: tag !== null,
        reuseRelease: release !== null,
    };
}

function buildReleasePreparationPlan(snapshot) {
    const readiness = validateMilestoneReadiness({
        activeMilestone: snapshot.activeMilestone,
        milestone: snapshot.milestone,
        issues: snapshot.issues,
    });
    const projectVersion = parseProjectVersion(snapshot.cmakeContents);
    const latestVersion = findLatestReleaseVersion(snapshot.tagNames);
    validateTargetVersion({targetVersion: readiness.version, projectVersion, latestVersion});
    validateRequiredChecks({
        masterSha: snapshot.masterSha,
        evaluatedSha: snapshot.evaluatedSha,
        requiredChecks: snapshot.requiredChecks,
        checks: snapshot.checks,
    });

    return {
        version: readiness.version,
        releaseIssue: readiness.releaseIssue,
        latestVersion,
        nextMilestone: selectNextMilestone(snapshot.milestones, snapshot.milestone.title),
        artifacts: planPreparationArtifacts({
            version: readiness.version,
            masterSha: snapshot.masterSha,
            branches: snapshot.branches,
            pullRequests: snapshot.pullRequests,
        }),
    };
}

module.exports = {
    buildReleasePreparationPlan,
    compareReleaseVersions,
    findLatestReleaseVersion,
    parseProjectVersion,
    parseMilestoneReleaseMetadata,
    parseReleaseTag,
    parseReleaseVersion,
    planFinalizationArtifacts,
    planPreparationArtifacts,
    releaseNames,
    roadmapMilestoneNumber,
    selectNextMilestone,
    validateMilestoneReadiness,
    validateRequiredChecks,
    validateTargetVersion,
};
