const fs = require('fs');
const readFile = (fileName) => util.promisify(fs.readFile)(fileName, 'utf8');
const util = require('util');
const jsExec = util.promisify(require("child_process").exec);

class Suggestion {
    constructor(file, startingLine, numberOfLines) {
        this.file = file;
        this.startingLine = startingLine;
        this.numberOfLines = numberOfLines;
        this.body = [];
    }

    addLine(line) {
        this.body.push(line);
    }

    getCommentBody() {
        return `\`\`\`suggestion\n${this.body.join('\n')}\`\`\``
    }
}

async function run() {
    console.log("Installing npm dependencies");
    const { stdout, stderr } = await jsExec("npm install @actions/core @actions/github");
    console.log("npm-install stderr:\n\n" + stderr);
    console.log("npm-install stdout:\n\n" + stdout);
    console.log("Finished installing npm dependencies");

    const github = require('@actions/github');
    const core = require('@actions/core');

    const octokit = github.getOctokit(core.getInput("auth_token", { required: true }));
    const diffFile = core.getInput("diff_file", { required: true });
    const reporter = core.getInput("reporter", { required: true });
    const formattedReporter = `**[${reporter}]**`;

    const maxSuggestionsInput = core.getInput("max_suggestions", { required: false });
    const runLocalCommand = core.getInput("run_local_command", { required: false });

    let maxSuggestions = undefined;
    if (maxSuggestionsInput) {
        maxSuggestions = parseInt(maxSuggestionsInput);
        if (runLocalCommand === undefined) {
            throw new Error("If a maximum number of suggestions is set, a run local command must also be provided.")
        }
    }

    const repoOwner = github.context.payload.repository.owner.login;
    const repoName = github.context.payload.repository.name;

    const triggeringPr = github.context.payload.workflow_run.pull_requests[0];
    const prNumber = triggeringPr.number;
    const commitId = triggeringPr.head.sha;

    try {
        const suggestions = await getAllSuggestions(diffFile);
        await submitSuggestions(octokit, prNumber, commitId, repoOwner, repoName, formattedReporter, maxSuggestions, runLocalCommand, suggestions);
    } catch (error) {
        core.setFailed(error);

        let messageBody = `${formattedReporter} Was unable to create all linter suggestions, for more details see https://github.com/${repoOwner}/${repoName}/actions/runs/${process.env.GITHUB_RUN_ID}`;
        if (runLocalCommand) {
            messageBody += `

To run the linter locally, please use: \`${runLocalCommand}\``;
        }

        await octokit.rest.issues.createComment({
            owner: repoOwner,
            repo: repoName,
            issue_number: prNumber,
            commit_id: commitId,
            body:messageBody});
    }
}

async function submitSuggestions(octokit, prNumber, commitId, owner, repo, reporter, maxSuggestions, runLocalCommand, suggestions) {
    if (maxSuggestions !== undefined && suggestions.length >= maxSuggestions) {
        await octokit.rest.issues.createComment({
            owner: owner,
            repo: repo,
            issue_number: prNumber,
            commit_id: commitId,
            body:`${reporter} is reporting too many linter changes (${suggestions.length}), please fix them locally and update this PR.

To fix them locally, please run: \`${runLocalCommand}\``});

        throw new Error(`Too many suggestions ${suggestions.length}/${maxSuggestions}`)
    }

    for (const suggestion of suggestions) {
        // https://docs.github.com/en/rest/pulls/comments?apiVersion=2022-11-28#create-a-review-comment-for-a-pull-request
        await octokit.rest.pulls.createReviewComment({
            owner: owner,
            repo: repo,
            pull_number: prNumber,
            commit_id: commitId,
            path: suggestion.file,
            start_line: suggestion.startingLine,
            line: suggestion.startingLine + suggestion.numberOfLines,
            start_side: 'RIGHT',
            side: 'RIGHT',
            body: `[${reporter}]\n${suggestion.getCommentBody()}`
        });
    }
}

async function getAllSuggestions(diffFile) {
    let diffContents = await readFile(diffFile);

    let allSuggestions = [];
    let currentSuggestion = undefined;

    let srcFile = undefined;
    let dstFile = undefined;

    let inFile = false;
    let inHunk = false;
    let hasContext = false;

    const srcFilePrefix = "--- ";
    const dstFilePrefix = "+++ ";

    const contextPrefix = " ";
    const delPrefix = "-";
    const addPrefix = "+";

    // https://www.gnu.org/software/diffutils/manual/html_node/index.html
    const hunkPrefix = "@@ ";
    const hunkRegex=/^@@ -(?<srcLine>\d+),?(?<srcLength>\d+)* \+(?<dstLine>\d+),?(?<dstLength>\d+)? @@/m

    const diffLines = diffContents.split(/\r?\n/);
    for (const line of diffLines)
    {
        if (inHunk) {
            if (line.startsWith(contextPrefix)) {
                hasContext = true;
                currentSuggestion.addLine(line.substring(contextPrefix.length));
                return;
            } else if (line.startsWith(delPrefix)) {
                // no-op
                return;
            } else if (line.startsWith(addPrefix)) {
                currentSuggestion.addLine(line.substring(addPrefix.length));
                return;
            } else {
                // Finished the hunk, save it and proceed with the line processing
                if (!hasContext) {
                    throw new Error("At least 1 line of context is required in the diff");
                }
                allSuggestions.push(currentSuggestion);
                console.log(currentSuggestion);
                currentSuggestion = undefined;
                inHunk = false;
            }
        }

        if (line.startsWith(srcFilePrefix)) {
            srcFile = line.substring(srcFilePrefix.length).trim();
            inFile = false;
        } else if (line.startsWith(dstFilePrefix)) {
            dstFile = line.substring(dstFilePrefix.length).trim();
            if (dstFile !== srcFile) {
                throw new Error(`The source and destination files are different! The diff must not contain prefixes or file renames. (src: ${srcFile} dst:${dstFile}`)
            }
            inFile = true;
        } else if (line.startsWith(hunkPrefix)) {
            if (!inFile) {
                throw new Error("Invalid diff file.")
            }

            inHunk = true;
            hasContext = false;
            const match = line.match(hunkRegex);

            const startingLine = parseInt(match.groups.srcLine.trim());
            const numberOfLines = (match.groups.srcLength === undefined) ? 0 : parseInt(match.groups.srcLength?.trim());

            currentSuggestion = new Suggestion(dstFile, startingLine, numberOfLines);
        }
    }

    return allSuggestions;
}

run();
