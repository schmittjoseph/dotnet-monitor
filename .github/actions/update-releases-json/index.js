const actionUtils = require('../action-utils.js');
const path = require('path');

async function run() {
    const [core, github] = await actionUtils.installAndRequirePackages("@actions/core", "@actions/github");

    const octokit = github.getOctokit(core.getInput("auth_token", { required: true }));

    const versionsDataFile = core.getInput("releases_json_file", { required: true });
    const endOfSupportDiscussionCategory = core.getInput("end_of_support_discussion_category", { required: false });
    const supportedFrameworks = core.getInput("supported_frameworks", { required: false });

    const repoOwner = github.context.payload.repository.owner.login;
    const repoName = github.context.payload.repository.name;

    const releasePayload = github.context.payload.release;

    try {
        const versionsData = JSON.parse(await actionUtils.readFile(versionsDataFile));

        if (releasePayload !== undefined) {
            const deprecatedRelease = addNewReleaseAndDeprecatePriorVersion(releasePayload, supportedFrameworks, versionsData);
            if (endOfSupportDiscussionCategory !== undefined && deprecatedRelease !== undefined) {
                await announceVersionHasEndOfSupport(octokit, endOfSupportDiscussionCategory, repoName, repoOwner, deprecatedRelease);
            }

        }

        cleanupPreviewVersions(versionsData);
        cleanupSupportedVersions(versionsData);
        cleanupUnsupportedVersions(versionsData);

        // Save to disk.
        await actionUtils.writeFile(versionsDataFile, JSON.stringify(versionsData, null, 2));
    } catch (error) {
        core.setFailed(error);
    }
}

function cleanupPreviewVersions(versionsData) {
    let versionsStillInPreview = [];

    for (const releaseKey of versionsData.preview) {
        const releaseData = versionsData.releases[releaseKey];
        const [_, __, ___, iteration] = actionUtils.splitVersionTag(releaseData.tag);
        if (iteration !== undefined) {
            versionsStillInPreview.push(releaseKey);
        }
    }

    versionsData.preview = versionsStillInPreview;
}

function cleanupSupportedVersions(versionsData) {
    const currentDate = new Date();
    let stillSupportedVersion = [];

    for (const releaseKey of versionsData.supported) {
        const releaseData = versionsData.releases[releaseKey];
        if (releaseData.outOfSupportDate === undefined) {
            stillSupportedVersion.unshift(releaseKey);
            continue;
        }

        const endOfSupportDate = new Date(releaseData.outOfSupportDate);
        if (currentDate >= endOfSupportDate) {
            versionsData.unsupported.unshift(releaseKey);
        } else {
            stillSupportedVersion.push(releaseKey);
        }
    }

    versionsData.supported = stillSupportedVersion;
}

function cleanupUnsupportedVersions(versionsData) {
    const currentDate = new Date();
    let versionsToStillMention = [];

    for (const releaseKey of versionsData.unsupported) {
        const releaseData = versionsData.releases[releaseKey];

        const dateToNoLongerMention = new Date(releaseData.outOfSupportDate);
        dateToNoLongerMention.setMonth(dateToNoLongerMention.getMonth() + versionsData.policy.cleanupUnsupportedReleasesAfterMonths);

        if (currentDate >= dateToNoLongerMention) {
            delete versionsData.releases[releaseKey];
        } else {
            versionsToStillMention.push(releaseKey);
        }
    }

    versionsData.unsupported = versionsToStillMention;
}

// Returns the release that is now out-of-support, if any.
function addNewReleaseAndDeprecatePriorVersion(releasePayload, supportedFrameworks, versionsData) {
    const releaseDate = new Date(releasePayload.published_at);
    // To keep things simple mark the release date as midnight.
    releaseDate.setHours(0, 0, 0, 0);

    const [majorVersion, minorVersion, patchVersion, iteration] = actionUtils.splitVersionTag(releasePayload.tag_name);

    const releaseMajorMinorVersion = `${majorVersion}.${minorVersion}`;

    // See if we're updating a release
    const existingRelease = versionsData.releases[releaseMajorMinorVersion];

    // Check if we're promoting a preview to RTM, if so re-create everything
    let isPromotion = false;
    if (iteration === undefined) {
        const [_, __, ___, existingIteration] = actionUtils.splitVersionTag(releasePayload.tag_name);
        if (existingIteration !== undefined) {
            isPromotion = true;
        }
    }

    const newRelease = {
        tag: releasePayload.tag_name,
        minorReleaseDate: releaseDate.toISOString(),
        patchReleaseDate: releaseDate.toISOString(),
        supportedFrameworks: supportedFrameworks.split(' ')
    };

    if (existingRelease === undefined || isPromotion === true) {
        if (iteration !== undefined) {
            versionsData.preview.push(releaseMajorMinorVersion);
        } else {
            versionsData.supported.push(releaseMajorMinorVersion);
        }
    } else if (iteration !== undefined) {
        newRelease.minorReleaseDate = existingRelease.minorReleaseDate;
    }

    versionsData.releases[releaseMajorMinorVersion] = newRelease;

    // Check if we're going to be putting a version out-of-support.
    if (minorVersion > 0 && patchVersion === 0 && iteration === undefined) {
        const endOfSupportDate = new Date(releaseDate.valueOf());
        endOfSupportDate.setMonth(endOfSupportDate.getMonth() + versionsData.policy.additionalMonthsOfSupportOnNewMinorRelease);

        const previousMinorReleaseKey = `${majorVersion}.${minorVersion-1}`;
        versionsData.releases[previousMinorReleaseKey].outOfSupportDate = endOfSupportDate;
        return versionsData.releases[previousMinorReleaseKey];
    }

    return undefined;
}

async function announceVersionHasEndOfSupport(ocotkit, category, repoName, repoOwner, version) {
    // There's currently no REST API for creating a discussion,
    // however we can use the GraphQL API to do so.

    // Get the repository id and map the category name to an id.
    const result = await ocotkit.graphql(`
    query ($repoName: String!, $owner: String!) {
        repository(name: $repoName, owner: $owner) {
          id
          discussionCategories (first: 50) {
            edges {
              node {
                id
                name
              }
            }
          }
        }
      }`,
    {
        owner: repoOwner,
        repoName: repoName
    });

    const repositoryId = result.repository.id;
    let categoryId = undefined;

    for (const edge of result.repository.discussionCategories.edges) {
        if (edge.node.name === category) {
            categoryId = edge.node.id;
            break;
        }
    }

    if (categoryId === undefined) {
        throw new Error(`Unable to determine category id for category ${category}`);
    }

    let discussionBody = await actionUtils.readFile(path.join(__dirname, "end_of_support_discussion.template.md"));
    const [major, minor] = actionUtils.splitVersionTag(version.tag);
    const friendlyDate = actionUtils.friendlyDateFromISODate(version.outOfSupportDate);

    const title =  `${major}.${minor}.X End of Support On ${friendlyDate}`;

    discussionBody = discussionBody.replace("${endOfSupportDate}", friendlyDate);
    discussionBody = discussionBody.replace("${majorMinorVersion}", `${major}.${minor}`); // todo: strio

    // https://docs.github.com/en/graphql/reference/mutations#creatediscussion
    // https://docs.github.com/en/graphql/reference/input-objects#creatediscussioninput
    const createDiscussionResult = await ocotkit.graphql(`
        mutation CreateDiscussion($repositoryId: ID!, $title: String!, $body: String!, $categoryId: ID!) {
            createDiscussion(input: {
                repositoryId: $repositoryId,
                categoryId: $categoryId,
                title: $title,
                body: $body
            }) {
                discussion {
                    url
                }
            }
        }`,
    {
        repositoryId: repositoryId,
        categoryId: categoryId,
        title: title,
        body: discussionBody
    });

    const discussionUrl = createDiscussionResult.createDiscussion.discussion.url;

    return discussionUrl;
}

run();
