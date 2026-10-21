/*
 * PSVitaAlive Store
 *
 * Homebrew application links visual override.
 *
 * Loaded after app-page.js.
 *
 * Changes:
 * - Uses link.name before link.type.
 * - Keeps the existing links[] schema.
 * - Displays links as cards, matching the visual style
 *   used by PS Vita / PSP / PS1 game links.
 * - Clearly labels the recommended link.
 * - Displays link-specific size when provided.
 * - Falls back to the application size for downloadable links.
 */

function getHomebrewLinkLabel(link) {

    if (link && link.name) {
        return String(link.name);
    }

    if (link && link.label) {
        return String(link.label);
    }

    if (link && link.title) {
        return String(link.title);
    }

    if (link && link.type) {
        return String(link.type);
    }

    return "Open link";
}


function getHomebrewLinkType(link) {

    return String(
        link && link.type
            ? link.type
            : "Other"
    );
}


function isDownloadableHomebrewLink(link) {

    const type = getHomebrewLinkType(link)
        .trim()
        .toLowerCase();

    return (
        type === "download" ||
        type === "mirror" ||
        type === "data files" ||
        type === "data file" ||
        type === "game files" ||
        type === "game file" ||
        type === "pkg"
    );
}


function getHomebrewLinkSize(link, app) {

    const linkSize =
        link && link.size !== undefined && link.size !== null
            ? Number(link.size)
            : NaN;

    if (
        Number.isFinite(linkSize) &&
        linkSize > 0
    ) {
        return linkSize;
    }

    if (isDownloadableHomebrewLink(link)) {

        const appSize =
            app && app.size !== undefined && app.size !== null
                ? Number(app.size)
                : NaN;

        if (
            Number.isFinite(appSize) &&
            appSize > 0
        ) {
            return appSize;
        }
    }

    return null;
}


function renderLinks(app) {

    const section =
        document.getElementById(
            "links-section"
        );

    const container =
        document.getElementById(
            "app-links"
        );

    if (!section || !container) {
        return;
    }

    container.innerHTML = "";

    if (
        !Array.isArray(app.links) ||
        app.links.length === 0
    ) {

        section.hidden = true;

        return;
    }

    section.hidden = false;

    const links = [
        ...app.links
    ];

    /*
     * Recommended links remain first,
     * without changing the source JSON.
     */
    links.sort(
        (a, b) => {

            if (
                a.recommended === true &&
                b.recommended !== true
            ) {
                return -1;
            }

            if (
                a.recommended !== true &&
                b.recommended === true
            ) {
                return 1;
            }

            return 0;
        }
    );

    links.forEach(
        linkData => {

            if (!linkData || !linkData.url) {
                return;
            }

            const link =
                document.createElement(
                    "a"
                );

            link.href =
                resolveDownloadUrl(linkData.url);

            link.target =
                "_blank";

            link.rel =
                "noopener noreferrer";

            link.className =
                "app-link-card";

            const isRecommended =
                linkData.recommended === true;

            if (isRecommended) {

                link.classList.add(
                    "recommended"
                );
            }

            const type =
                document.createElement(
                    "span"
                );

            type.className =
                "app-link-card-type";

            type.textContent =
                getHomebrewLinkType(
                    linkData
                );

            const name =
                document.createElement(
                    "span"
                );

            name.className =
                "app-link-card-name";

            /*
             * IMPORTANT:
             * The visible title is NAME,
             * not TYPE.
             */
            name.textContent =
                getHomebrewLinkLabel(
                    linkData
                );

            // The type and Recommended label flow side by side.
            const header = document.createElement("div");
            header.className = "app-link-card-header";
            header.appendChild(type);

            if (isRecommended) {
                const recommended = document.createElement("span");
                recommended.className = "app-link-card-recommended";
                recommended.textContent = "Recommended";
                header.appendChild(recommended);
            }

            link.appendChild(header);
            link.appendChild(name);

            const metaParts = [];
            if (linkData.region) metaParts.push(String(linkData.region));
            if (linkData.title_id) metaParts.push(String(linkData.title_id));
            if (linkData.description) metaParts.push(String(linkData.description));

            if (metaParts.length > 0) {
                const meta = document.createElement("span");
                meta.className = "app-link-card-meta";
                meta.textContent = metaParts.join(" · ");
                link.appendChild(meta);
            }

            const size =
                getHomebrewLinkSize(
                    linkData,
                    app
                );

            if (size !== null) {

                const sizeElement =
                    document.createElement(
                        "span"
                    );

                sizeElement.className =
                    "app-link-card-size";

                sizeElement.textContent =
                    `Size: ${formatFileSize(size)}`;

                link.appendChild(
                    sizeElement
                );
            }

            // Only show destinations explicitly present on this link.
            // A Plugin uses the destination to copy its .skprx/.suprx file.
            const isPlugin =
                getHomebrewLinkType(linkData).trim().toLowerCase() === "plugin";
            const extractPath = typeof linkData.extract_path === "string"
                ? linkData.extract_path.trim() : "";
            const pluginSection = isPlugin && typeof linkData.section === "string"
                ? linkData.section.trim() : "";
            const pluginLine = isPlugin && typeof linkData.line === "string"
                ? linkData.line.trim() : "";

            const detailRows = [];
            if (extractPath) {
                detailRows.push([
                    isPlugin ? "Install to" : "Extract to",
                    extractPath
                ]);
            }
            if (pluginSection) detailRows.push(["Config", pluginSection]);
            if (pluginLine) detailRows.push(["Entry", pluginLine]);

            if (detailRows.length > 0) {
                const details = document.createElement("div");
                details.className = "app-link-card-details";
                detailRows.forEach(([labelText, valueText]) => {
                    const row = document.createElement("div");
                    row.className = "app-link-card-detail";

                    const label = document.createElement("span");
                    label.className = "app-link-card-detail-label";
                    label.textContent = labelText;

                    const value = document.createElement("code");
                    value.className = "app-link-card-detail-value";
                    value.textContent = valueText;

                    row.appendChild(label);
                    row.appendChild(value);
                    details.appendChild(row);
                });
                link.appendChild(details);
            }

            container.appendChild(
                link
            );
        }
    );
}
