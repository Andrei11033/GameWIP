'use strict';

const fs = require('node:fs');

const DEFAULT_REQUIRED_CHECKS = Object.freeze(['Validation']);
const MASTER_BRANCH = 'master';
const RELEASE_COMMANDS = Object.freeze(new Set(['check', 'prepare', 'finalize']));
const RELEASE_ISSUE_LABEL = 'type:release';
const RELEASE_ISSUE_TITLE_PATTERN = /^release:/i;

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

function issueLabelNames(issue) {
    if (!Array.isArray(issue.labels)) {
        return [];
    }

    return issue.labels
        .map((label) => {
            if (typeof label === 'string') {
                return label;
            }
            return String(label?.name ?? '');
        })
        .filter((name) => name.length > 0);
}

function isAutomaticReleaseIssueCandidate(issue) {
    const title = String(issue.title ?? '');
    return issueLabelNames(issue).includes(RELEASE_ISSUE_LABEL) || RELEASE_ISSUE_TITLE_PATTERN.test(title);
}

function resolveReleaseIssue({ milestone, metadataReleaseIssue, issues }) {
    if (metadataReleaseIssue !== null) {
        const releaseIssues = issues.filter((issue) => issue.number === metadataReleaseIssue);
        if (releaseIssues.length !== 1) {
            throw new Error(`Milestone '${milestone.title}' must contain release issue #${metadataReleaseIssue} exactly once.`);
        }
        return releaseIssues[0];
    }

    const releaseIssues = issues.filter(isAutomaticReleaseIssueCandidate);
    if (releaseIssues.length !== 1) {
        const suffix = releaseIssues.length === 0 ? 'none were found' : `found ${releaseIssues.map((issue) => `#${issue.number}`).join(', ')}`;
        throw new Error(
            `Milestone '${milestone.title}' must have exactly one release issue labeled '${RELEASE_ISSUE_LABEL}' or titled 'release: ...'; ${suffix}.`,
        );
    }
    return releaseIssues[0];
}

function validateTargetVersion({ targetVersion, projectVersion, latestVersion }) {
    if (compareReleaseVersions(targetVersion, projectVersion) !== 0) {
        throw new Error(`Milestone target ${targetVersion.text} does not match root project version ${projectVersion.text}.`);
    }
    if (latestVersion !== null && compareReleaseVersions(targetVersion, latestVersion) <= 0) {
        throw new Error(`Milestone target ${targetVersion.text} must be newer than latest release ${latestVersion.text}.`);
    }
    return true;
}

function validateMilestoneReadiness({ activeMilestone, milestone, issues }) {
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
    const releaseIssue = resolveReleaseIssue({
        milestone,
        metadataReleaseIssue: metadata.releaseIssue,
        issues,
    });
    if (String(releaseIssue.state).toLowerCase() !== 'open') {
        throw new Error(`Release issue #${releaseIssue.number} must remain open during preparation.`);
    }

    const otherOpenIssues = issues
        .filter((issue) => issue.number !== releaseIssue.number && String(issue.state).toLowerCase() === 'open')
        .map((issue) => issue.number)
        .sort((left, right) => left - right);
    if (otherOpenIssues.length > 0) {
        throw new Error(`Milestone '${milestone.title}' still has open implementation issues: ${otherOpenIssues.join(', ')}.`);
    }

    return {
        version: metadata.version,
        releaseIssue,
    };
}

function validateRequiredChecks({ masterSha, evaluatedSha, requiredChecks, checks }) {
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
        throw new Error(`Expected exactly one R${String(expectedNumber).padStart(2, '0')} milestone, found ${matches.length}.`);
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

function releaseNotesPath(version) {
    return `docs/releases/v${version.text}.md`;
}

function planPreparationArtifacts({ version, masterSha, branches = [], pullRequests = [] }) {
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
        pullRequestNumber: pullRequest?.number ?? null,
        pullRequestMergeCommitSha: pullRequest?.mergeCommitSha ?? null,
    };
}

function planFinalizationArtifacts({ version, releaseCommitSha, tags = [], releases = [] }) {
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
    validateTargetVersion({ targetVersion: readiness.version, projectVersion, latestVersion });
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
        masterSha: snapshot.masterSha,
        nextMilestone: selectNextMilestone(snapshot.milestones, snapshot.milestone.title),
        artifacts: planPreparationArtifacts({
            version: readiness.version,
            masterSha: snapshot.masterSha,
            branches: snapshot.branches,
            pullRequests: snapshot.pullRequests,
        }),
    };
}

function releaseNotesTemplate({ version, milestoneTitle, releaseIssue, nextMilestoneTitle }) {
    return [
        `# GameWIP v${version.text} release notes`,
        '',
        `Milestone: ${milestoneTitle}`,
        `Release issue: #${releaseIssue.number}`,
        nextMilestoneTitle === null ? 'Next milestone: none' : `Next milestone: ${nextMilestoneTitle}`,
        '',
        '## Summary',
        '',
        'Summarize the release scope and user-visible changes here before merging this release-preparation pull request.',
        '',
        '## Validation evidence',
        '',
        'Paste the final validation commands, environment, results, skips, and observed manual UI behavior here before merging this release-preparation pull request.',
        '',
        '## Release checks',
        '',
        `- [ ] Root \`project(GameWIP VERSION ...)\` matches \`${version.text}\`.`,
        '- [ ] Required validation workflows pass on the release-preparation pull request.',
        '- [ ] The release-preparation pull request is human-reviewed and merged.',
        '- [ ] Post-merge validation passes on `master`.',
        `- [ ] Annotated tag \`v${version.text}\` is created on the verified merge commit.`,
        `- [ ] GitHub release \`GameWIP v${version.text}\` points at \`v${version.text}\`.`,
        '',
    ].join('\n');
}

function releasePullRequestBody(plan) {
    const latestVersion = plan.latestVersion === null ? 'none' : plan.latestVersion.text;
    const nextMilestone = plan.nextMilestone === null ? 'none' : plan.nextMilestone.title;
    return [
        `Prepares the guarded release notes for \`${plan.artifacts.tagName}\`.`,
        '',
        'This pull request is generated by the release-preparation workflow. It intentionally requires a human review and merge before any tag or GitHub release is created.',
        '',
        '## Release target',
        '',
        `- Target version: \`${plan.version.text}\``,
        `- Latest existing release: \`${latestVersion}\``,
        `- Release issue: #${plan.releaseIssue.number}`,
        `- Next milestone: \`${nextMilestone}\``,
        '',
        '## Required maintainer actions',
        '',
        '- Fill in the final validation evidence in the release notes file.',
        '- Verify the pull request checks pass.',
        '- Merge this pull request manually when this release is ready to publish.',
        '- Run release finalization only after the post-merge `master` checks pass.',
        '',
        `Refs #${plan.releaseIssue.number}`,
        '',
    ].join('\n');
}

function releaseBodyFromNotes(notes) {
    const lines = String(notes).replace(/\r\n?/g, '\n').split('\n');
    return lines
        .filter((line) => !line.startsWith('- [ ] '))
        .join('\n')
        .trim();
}

function validateReleaseNotesReady(notes) {
    const normalized = String(notes).replace(/\r\n?/g, '\n');
    if (normalized.includes('Paste the final validation commands, environment, results, skips, and observed manual UI behavior here')) {
        throw new Error('Release notes still contain the generated validation-evidence placeholder.');
    }
    if (/^- \[ \] /m.test(normalized)) {
        throw new Error('Release notes still contain unchecked release checklist items.');
    }
    return true;
}

function parseBoolean(value) {
    return String(value ?? '').toLowerCase() === 'true';
}

function splitRepository(nameWithOwner) {
    const [owner, repository] = String(nameWithOwner ?? '').split('/');
    if (!owner || !repository) {
        throw new Error(`Invalid repository name: ${nameWithOwner}`);
    }
    return { owner, repository };
}

function readRequiredChecks(value) {
    const requiredChecks = String(value ?? '')
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line.length > 0);

    return requiredChecks.length > 0 ? requiredChecks : [...DEFAULT_REQUIRED_CHECKS];
}

function readReleaseConfiguration(context) {
    const activeMilestone = String(process.env.ACTIVE_MILESTONE ?? '').trim();
    if (activeMilestone.length === 0) {
        throw new Error('ACTIVE_MILESTONE must be configured.');
    }

    const command = String(process.env.RELEASE_COMMAND ?? 'check').trim();
    if (!RELEASE_COMMANDS.has(command)) {
        throw new Error(`Unsupported release command: ${command}`);
    }

    return {
        ...splitRepository(process.env.GITHUB_REPOSITORY ?? context.payload.repository?.full_name),
        activeMilestone,
        command,
        dryRun: parseBoolean(process.env.DRY_RUN ?? 'true'),
        releaseCommit: String(process.env.RELEASE_COMMIT ?? '').trim() || null,
        allowNotReady: parseBoolean(process.env.ALLOW_NOT_READY),
        requiredChecks: readRequiredChecks(process.env.REQUIRED_CHECKS),
    };
}

async function listMilestones(github, owner, repository) {
    return github.paginate(github.rest.issues.listMilestones, {
        owner,
        repo: repository,
        state: 'all',
        per_page: 100,
    });
}

async function listMilestoneIssues(github, owner, repository, milestoneNumber) {
    const items = await github.paginate(github.rest.issues.listForRepo, {
        owner,
        repo: repository,
        milestone: milestoneNumber,
        state: 'all',
        per_page: 100,
    });

    return items
        .filter((item) => item.pull_request === undefined)
        .map((issue) => ({
            number: issue.number,
            state: issue.state,
            title: issue.title,
            labels: issue.labels.map((label) => label.name),
        }));
}

async function listBranches(github, owner, repository) {
    const branches = await github.paginate(github.rest.repos.listBranches, {
        owner,
        repo: repository,
        per_page: 100,
    });

    return branches.map((branch) => ({
        name: branch.name,
        sha: branch.commit.sha,
    }));
}

async function mergeBaseForPullRequest(github, owner, repository, masterSha, headSha) {
    const comparison = await github.rest.repos.compareCommitsWithBasehead({
        owner,
        repo: repository,
        basehead: `${masterSha}...${headSha}`,
    });

    return comparison.data.merge_base_commit?.sha ?? null;
}

async function listPullRequests(github, owner, repository, masterSha) {
    const pullRequests = await github.paginate(github.rest.pulls.list, {
        owner,
        repo: repository,
        state: 'all',
        base: MASTER_BRANCH,
        per_page: 100,
    });

    const results = [];
    for (const pullRequest of pullRequests) {
        let mergeBaseSha = pullRequest.base.sha;
        if (pullRequest.head.repo?.full_name === `${owner}/${repository}`) {
            mergeBaseSha = await mergeBaseForPullRequest(github, owner, repository, masterSha, pullRequest.head.sha);
        }

        results.push({
            number: pullRequest.number,
            headRefName: pullRequest.head.ref,
            headSha: pullRequest.head.sha,
            baseRefName: pullRequest.base.ref,
            mergeBaseSha,
            mergeCommitSha: pullRequest.merge_commit_sha,
            state: pullRequest.merged_at === null ? pullRequest.state : 'merged',
        });
    }

    return results;
}

async function listReleaseTags(github, owner, repository) {
    const refs = await github.paginate(github.rest.git.listMatchingRefs, {
        owner,
        repo: repository,
        ref: 'tags/v',
        per_page: 100,
    });

    const tags = [];
    for (const ref of refs) {
        const name = ref.ref.replace(/^refs\/tags\//, '');
        const annotated = ref.object.type === 'tag';
        let targetSha = ref.object.sha;

        if (annotated) {
            const tag = await github.rest.git.getTag({
                owner,
                repo: repository,
                tag_sha: ref.object.sha,
            });
            targetSha = tag.data.object.sha;
        }

        tags.push({ name, annotated, targetSha });
    }

    return tags;
}

async function listReleases(github, owner, repository) {
    const releases = await github.paginate(github.rest.repos.listReleases, {
        owner,
        repo: repository,
        per_page: 100,
    });

    return releases.map((release) => ({
        id: release.id,
        tagName: release.tag_name,
        draft: release.draft,
    }));
}

async function readMasterCommit(github, owner, repository) {
    const branch = await github.rest.repos.getBranch({
        owner,
        repo: repository,
        branch: MASTER_BRANCH,
    });

    return branch.data.commit.sha;
}

async function readWorkflowChecks(github, owner, repository, masterSha, requiredChecks) {
    const workflowRuns = await github.paginate(github.rest.actions.listWorkflowRunsForRepo, {
        owner,
        repo: repository,
        branch: MASTER_BRANCH,
        per_page: 100,
    });

    const checks = {};
    for (const name of requiredChecks) {
        const matchingRuns = workflowRuns
            .filter((run) => run.name === name && run.head_sha === masterSha)
            .sort((left, right) => new Date(right.created_at) - new Date(left.created_at));

        if (matchingRuns.length > 0) {
            checks[name] = {
                status: matchingRuns[0].status,
                conclusion: matchingRuns[0].conclusion,
                headSha: matchingRuns[0].head_sha,
            };
        }
    }

    return checks;
}

async function buildGitHubSnapshot(github, config) {
    const masterSha = await readMasterCommit(github, config.owner, config.repository);
    const milestones = await listMilestones(github, config.owner, config.repository);
    const milestone = milestones.find((candidate) => candidate.title === config.activeMilestone);
    if (milestone === undefined) {
        throw new Error(`Active milestone was not found: ${config.activeMilestone}`);
    }

    const [issues, branches, pullRequests, tags, releases, checks] = await Promise.all([
        listMilestoneIssues(github, config.owner, config.repository, milestone.number),
        listBranches(github, config.owner, config.repository),
        listPullRequests(github, config.owner, config.repository, masterSha),
        listReleaseTags(github, config.owner, config.repository),
        listReleases(github, config.owner, config.repository),
        readWorkflowChecks(github, config.owner, config.repository, masterSha, config.requiredChecks),
    ]);

    return {
        activeMilestone: config.activeMilestone,
        milestone: {
            title: milestone.title,
            state: milestone.state,
            description: milestone.description ?? '',
        },
        issues,
        cmakeContents: fs.readFileSync('CMakeLists.txt', 'utf8'),
        tagNames: tags.map((tag) => tag.name),
        tags,
        releases,
        masterSha,
        evaluatedSha: masterSha,
        requiredChecks: config.requiredChecks,
        checks,
        milestones: milestones.map((candidate) => ({ title: candidate.title })),
        branches,
        pullRequests,
    };
}

function releasePlanMarkdown(config, plan) {
    const latestVersion = plan.latestVersion === null ? 'none' : plan.latestVersion.text;
    const nextMilestone = plan.nextMilestone === null ? 'none' : plan.nextMilestone.title;
    return [
        '# Release preparation',
        '',
        `- Command: \`${config.command}\``,
        `- Dry run: \`${config.dryRun}\``,
        `- Active milestone: \`${config.activeMilestone}\``,
        `- Target version: \`${plan.version.text}\``,
        `- Latest release version: \`${latestVersion}\``,
        `- Release issue: #${plan.releaseIssue.number}`,
        `- Next milestone: \`${nextMilestone}\``,
        `- Branch: \`${plan.artifacts.branchName}\``,
        `- Pull request title: \`${plan.artifacts.pullRequestTitle}\``,
        '',
        '## Preparation artifacts',
        '',
        `- Create branch: \`${plan.artifacts.createBranch}\``,
        `- Create pull request: \`${plan.artifacts.createPullRequest}\``,
        `- Reuse branch: \`${plan.artifacts.reuseBranch}\``,
        `- Reuse pull request: \`${plan.artifacts.reusePullRequest}\``,
    ].join('\n');
}

function finalizationPlanMarkdown(config, preparationPlan, finalizationPlan) {
    return [
        releasePlanMarkdown(config, preparationPlan),
        '',
        '## Finalization artifacts',
        '',
        `- Release commit: \`${config.releaseCommit}\``,
        `- Tag: \`${finalizationPlan.tagName}\``,
        `- Release name: \`${finalizationPlan.releaseName}\``,
        `- Create tag: \`${finalizationPlan.createTag}\``,
        `- Create release: \`${finalizationPlan.createRelease}\``,
        `- Publish draft release: \`${finalizationPlan.publishDraftRelease}\``,
        `- Reuse tag: \`${finalizationPlan.reuseTag}\``,
        `- Reuse release: \`${finalizationPlan.reuseRelease}\``,
    ].join('\n');
}

async function writeSummary(core, markdown) {
    if (core.summary?.addRaw) {
        await core.summary.addRaw(`${markdown}\n`).write();
    }
    core.info(markdown);
}

async function getRepositoryFile(github, owner, repository, path, ref) {
    try {
        const file = await github.rest.repos.getContent({
            owner,
            repo: repository,
            path,
            ref,
        });
        if (Array.isArray(file.data) || file.data.type !== 'file') {
            throw new Error(`Expected ${path} to be a file.`);
        }
        return file.data;
    } catch (error) {
        if (error.status === 404) {
            return null;
        }
        throw error;
    }
}

function encodeBase64(value) {
    return Buffer.from(value, 'utf8').toString('base64');
}

async function createReleaseBranch(github, config, plan) {
    if (!plan.artifacts.createBranch) {
        return;
    }

    await github.rest.git.createRef({
        owner: config.owner,
        repo: config.repository,
        ref: `refs/heads/${plan.artifacts.branchName}`,
        sha: plan.masterSha,
    });
}

async function ensureReleaseNotesFile(github, core, config, plan) {
    if (plan.artifacts.pullRequestMerged) {
        core.info('Release pull request is already merged; release notes file is not changed.');
        return;
    }

    const path = releaseNotesPath(plan.version);
    const existing = await getRepositoryFile(github, config.owner, config.repository, path, plan.artifacts.branchName);
    if (existing !== null) {
        core.info(`${path} already exists on ${plan.artifacts.branchName}; preserving existing content.`);
        return;
    }

    const nextMilestoneTitle = plan.nextMilestone === null ? null : plan.nextMilestone.title;
    await github.rest.repos.createOrUpdateFileContents({
        owner: config.owner,
        repo: config.repository,
        path,
        branch: plan.artifacts.branchName,
        message: `docs: add ${plan.artifacts.tagName} release notes`,
        content: encodeBase64(
            releaseNotesTemplate({
                version: plan.version,
                milestoneTitle: config.activeMilestone,
                releaseIssue: plan.releaseIssue,
                nextMilestoneTitle,
            }),
        ),
    });
}

async function ensureReleasePullRequest(github, core, config, plan) {
    if (plan.artifacts.pullRequestMerged) {
        core.info(`Release pull request #${plan.artifacts.pullRequestNumber} is already merged.`);
        return;
    }
    if (!plan.artifacts.createPullRequest) {
        core.info(`Release pull request #${plan.artifacts.pullRequestNumber} already exists.`);
        return;
    }

    await github.rest.pulls.create({
        owner: config.owner,
        repo: config.repository,
        title: plan.artifacts.pullRequestTitle,
        head: plan.artifacts.branchName,
        base: MASTER_BRANCH,
        body: releasePullRequestBody(plan),
        maintainer_can_modify: true,
    });
}

async function applyPreparationWrites({ github, core, config, plan }) {
    await createReleaseBranch(github, config, plan);
    await ensureReleaseNotesFile(github, core, config, plan);
    await ensureReleasePullRequest(github, core, config, plan);
}

function validateMergedReleasePullRequest(plan, releaseCommitSha) {
    if (!plan.artifacts.pullRequestMerged) {
        throw new Error(`Release pull request ${plan.artifacts.pullRequestTitle} must be merged before finalization.`);
    }
    if (plan.artifacts.pullRequestMergeCommitSha !== releaseCommitSha) {
        throw new Error(`Release pull request merge commit ${plan.artifacts.pullRequestMergeCommitSha} is not release commit ${releaseCommitSha}.`);
    }
    return true;
}

function readReleaseNotesForRelease(version) {
    const path = releaseNotesPath(version);
    if (!fs.existsSync(path)) {
        throw new Error(`Release notes file is missing: ${path}`);
    }
    return fs.readFileSync(path, 'utf8');
}

async function createAnnotatedReleaseTag(github, config, plan) {
    if (!plan.createTag) {
        return;
    }

    const tag = await github.rest.git.createTag({
        owner: config.owner,
        repo: config.repository,
        tag: plan.tagName,
        message: `${plan.releaseName}\n`,
        object: config.releaseCommit,
        type: 'commit',
    });

    await github.rest.git.createRef({
        owner: config.owner,
        repo: config.repository,
        ref: `refs/tags/${plan.tagName}`,
        sha: tag.data.sha,
    });
}

async function ensureGitHubRelease(github, config, finalizationPlan, releaseBody, releases) {
    const existingRelease = releases.find((release) => release.tagName === finalizationPlan.tagName) ?? null;
    if (finalizationPlan.createRelease) {
        await github.rest.repos.createRelease({
            owner: config.owner,
            repo: config.repository,
            tag_name: finalizationPlan.tagName,
            target_commitish: config.releaseCommit,
            name: finalizationPlan.releaseName,
            body: releaseBody,
            draft: false,
            prerelease: false,
        });
        return;
    }

    if (finalizationPlan.publishDraftRelease) {
        await github.rest.repos.updateRelease({
            owner: config.owner,
            repo: config.repository,
            release_id: existingRelease.id,
            draft: false,
            body: releaseBody,
            name: finalizationPlan.releaseName,
        });
    }
}

async function applyFinalizationWrites({ github, config, finalizationPlan, releaseBody, releases }) {
    await createAnnotatedReleaseTag(github, config, finalizationPlan);
    await ensureGitHubRelease(github, config, finalizationPlan, releaseBody, releases);
}

async function runPreparationCommand({ github, core, config }) {
    const snapshot = await buildGitHubSnapshot(github, config);
    const plan = buildReleasePreparationPlan(snapshot);
    await writeSummary(core, releasePlanMarkdown(config, plan));

    if (config.command === 'prepare' && !config.dryRun) {
        await applyPreparationWrites({ github, core, config, plan });
    }
}

async function runFinalizationCommand({ github, core, config }) {
    if (config.releaseCommit === null) {
        throw new Error('RELEASE_COMMIT is required for finalization.');
    }

    const snapshot = await buildGitHubSnapshot(github, config);
    const preparationPlan = buildReleasePreparationPlan(snapshot);
    if (config.releaseCommit !== snapshot.masterSha) {
        throw new Error(`Release commit ${config.releaseCommit} is not latest master commit ${snapshot.masterSha}.`);
    }

    const finalizationPlan = planFinalizationArtifacts({
        version: preparationPlan.version,
        releaseCommitSha: config.releaseCommit,
        tags: snapshot.tags,
        releases: snapshot.releases,
    });
    await writeSummary(core, finalizationPlanMarkdown(config, preparationPlan, finalizationPlan));
    validateMergedReleasePullRequest(preparationPlan, config.releaseCommit);
    const releaseNotes = readReleaseNotesForRelease(preparationPlan.version);
    validateReleaseNotesReady(releaseNotes);
    const releaseBody = releaseBodyFromNotes(releaseNotes);

    if (!config.dryRun) {
        await applyFinalizationWrites({
            github,
            config,
            finalizationPlan,
            releaseBody,
            releases: snapshot.releases,
        });
    }
}

async function run({ github, context, core }) {
    const config = readReleaseConfiguration(context);
    try {
        if (config.command === 'finalize') {
            await runFinalizationCommand({ github, core, config });
        } else {
            await runPreparationCommand({ github, core, config });
        }
    } catch (error) {
        if (!config.allowNotReady) {
            throw error;
        }

        const message = error instanceof Error ? error.message : String(error);
        core.warning(`Release is not ready: ${message}`);
        await writeSummary(
            core,
            ['# Release preparation', '', 'Release preparation reconciliation completed without writes.', '', `Reason: ${message}`].join('\n'),
        );
    }
}

module.exports = {
    DEFAULT_REQUIRED_CHECKS,
    buildReleasePreparationPlan,
    compareReleaseVersions,
    findLatestReleaseVersion,
    parseProjectVersion,
    parseMilestoneReleaseMetadata,
    parseReleaseTag,
    parseReleaseVersion,
    planFinalizationArtifacts,
    planPreparationArtifacts,
    releaseBodyFromNotes,
    releaseNotesPath,
    releaseNotesTemplate,
    releaseNames,
    releasePullRequestBody,
    run,
    roadmapMilestoneNumber,
    selectNextMilestone,
    validateMergedReleasePullRequest,
    validateMilestoneReadiness,
    validateReleaseNotesReady,
    validateRequiredChecks,
    validateTargetVersion,
};
