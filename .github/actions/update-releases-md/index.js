const actionUtils = require('../action-utils.js');

async function run() {
    const [core] = await actionUtils.installAndRequirePackages("@actions/core");

    const versionsDataFile = core.getInput("releases_json_file", { required: true });
    const outputFile = core.getInput("releases_md_file", { required: true });

    try {
        const versionsData = JSON.parse(await actionUtils.readFile(versionsDataFile));

        const releasesMdContent = generateReleasesMdContent(versionsData);

        await actionUtils.writeFile(outputFile, releasesMdContent);
    } catch (error) {
        core.setFailed(error);
    }
}

function generateReleasesMdContent(versionsData) {
    let supportedReleasesTable = '';
    let previewReleasesTable = '';
    let outOfSupportReleasesTable = '';

    for (const releaseKey of versionsData.supported) {
        const release = versionsData.releases[releaseKey];
        const [major, minor, patch, iteration] = actionUtils.splitVersionTag(release.tag);
        if (iteration !== undefined) {
            // It's a preview release
            previewReleasesTable += `${generateTableRow(release, false)}\n`;
        } else {
            // It's an RTM release
            supportedReleasesTable += `${generateTableRow(release, true)}\n`;
        }
    }

    for (const releaseKey of versionsData.unsupported) {
        const release = versionsData.releases[releaseKey];
        outOfSupportReleasesTable += `${generateTableRow(release, true)}\n`;
    }

    let content ='# Releases\n\n';

    if (supportedReleasesTable.length > 0) {
        content += `## Supported versions\n\n${generateTableHeader(true)}\n${supportedReleasesTable}\n\n`;
    }

    if (outOfSupportReleasesTable.length > 0) {
        content += `## Out of support versions\n\n${generateTableHeader(true)}\n${outOfSupportReleasesTable}\n\n`;
    }

    if (previewReleasesTable.length > 0) {
        content += `## Preview versions\n\n${generateTableHeader(false)}\n${previewReleasesTable}\n\n`;
    }


    return content;
}

function generateTableHeader(includeEndOfSupport) {
    let headers = ['Version', 'Original Release Date', 'Latest Patch Version', 'Patch Release Date'];
    if (includeEndOfSupport) {
        headers.push('End of Support');
    }
    headers.push('Runtime Frameworks');

    let headerString = `${convertArrayIntoTableRow(headers)}\n`;

    const seperators = Array(headers.length).fill('---');
    headerString += convertArrayIntoTableRow(seperators);

    return headerString;
}

function generateTableRow(release, includeEndOfSupport) {
    const [major, minor] = actionUtils.splitVersionTag(release.tag);

    let columns = [
        `${major}.${minor}`,
        actionUtils.friendlyDateFromISODate(release.minorReleaseDate),
        `[${release.currentVersion}](${release.htmlUrl})`,
        actionUtils.friendlyDateFromISODate(release.patchReleaseDate)
    ];

    if (includeEndOfSupport) {
        columns.push(actionUtils.friendlyDateFromISODate(release.outOfSupportDate));
    }

    columns.push(release.supportedFrameworks.join("<br/>"));

    return convertArrayIntoTableRow(columns);
}

function convertArrayIntoTableRow(array) {
    return `| ${array.join(' | ')} |`;
}

run();
