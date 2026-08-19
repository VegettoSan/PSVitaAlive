/**
 * PSVitaAlive Store
 *
 * Shared PS Vita / PSP / PS1 game detail page.
 */


/* ========================================
   URL
======================================== */

function getGameIdFromUrl() {

    const params =
        new URLSearchParams(
            window.location.search
        );

    return params.get("id");
}


/* ========================================
   Helpers
======================================== */

function gameDisplayValue(
    value,
    fallback = "Not available"
) {

    if (
        value === null ||
        value === undefined ||
        value === ""
    ) {
        return fallback;
    }

    return String(value);
}


function gameReleaseDate(date) {

    if (!date) {
        return "Unknown";
    }

    const parsed =
        new Date(date);

    if (
        Number.isNaN(
            parsed.getTime()
        )
    ) {
        return date;
    }

    return parsed.toLocaleDateString(
        undefined,
        {
            year: "numeric",
            month: "long",
            day: "numeric"
        }
    );
}


/* ========================================
   Load catalog
======================================== */

/*
 * multi-game-page.js overrides this function
 * for PSP / PS1 / PS Vita.
 */
async function loadOfficialGamesCatalog() {

    return loadJSON(
        `${VITAHUB_RAW_BASE}/catalog_psvita_games.json`
    );
}


/* ========================================
   Categories
======================================== */

function renderGameCategories(game) {

    const section =
        document.getElementById(
            "game-categories-section"
        );

    const container =
        document.getElementById(
            "game-categories"
        );

    container.innerHTML = "";

    const categories = [];

    if (game.category_id) {

        categories.push({
            id: game.category_id,
            type: "category"
        });

    }

    if (
        Array.isArray(
            game.subcategory_ids
        )
    ) {

        game.subcategory_ids.forEach(
            id => {

                categories.push({
                    id,
                    type: "subcategory"
                });

            }
        );

    }

    if (
        categories.length === 0
    ) {

        section.hidden = true;

        return;
    }

    section.hidden = false;

    categories.forEach(
        category => {

            const link =
                document.createElement(
                    "a"
                );

            link.className =
                "game-tag game-tag-link";

            link.href =
                `category.html?id=${encodeURIComponent(
                    category.id
                )}`;

            link.textContent =
                category.id;

            container.appendChild(
                link
            );

        }
    );
}


/* ========================================
   Title IDs
======================================== */

function renderGameTitleIds(game) {

    const section =
        document.getElementById(
            "title-ids-section"
        );

    const container =
        document.getElementById(
            "game-title-ids"
        );

    container.innerHTML = "";

    if (
        !game.title_ids ||
        typeof game.title_ids !== "object"
    ) {

        section.hidden = true;

        return;
    }

    const entries =
        Object.entries(
            game.title_ids
        );

    if (
        entries.length === 0
    ) {

        section.hidden = true;

        return;
    }

    section.hidden = false;

    entries.forEach(
        ([region, titleId]) => {

            if (!titleId) {
                return;
            }

            const item =
                document.createElement(
                    "div"
                );

            item.className =
                "game-title-id";

            const regionElement =
                document.createElement(
                    "span"
                );

            regionElement.className =
                "game-title-id-region";

            regionElement.textContent =
                region;

            const labelElement =
                document.createElement(
                    "span"
                );

            labelElement.className =
                "game-title-id-label";

            labelElement.textContent =
                "Title ID";

            const valueElement =
                document.createElement(
                    "span"
                );

            valueElement.className =
                "game-title-id-value";

            valueElement.textContent =
                titleId;

            item.appendChild(
                regionElement
            );

            item.appendChild(
                labelElement
            );

            item.appendChild(
                valueElement
            );

            container.appendChild(
                item
            );

        }
    );
}


/* ========================================
   Links
======================================== */

function getGameLinkType(link) {

    return String(
        link?.type ||
        link?.kind ||
        "Other"
    ).trim();
}


function getGameLinkLabel(link) {

    if (link?.name) {
        return String(
            link.name
        );
    }

    if (link?.label) {
        return String(
            link.label
        );
    }

    if (link?.title) {
        return String(
            link.title
        );
    }

    return getGameLinkType(
        link
    );
}


/*
 * Normalize the links array without
 * changing the source catalog.
 *
 * This keeps the official schema:
 *
 * game.links[]
 *
 * but makes the page tolerant of
 * harmless differences in type casing.
 */
function normalizeGameLinks(game) {

    if (
        !Array.isArray(
            game?.links
        )
    ) {
        return [];
    }

    return game.links
        .filter(
            link =>
                link &&
                typeof link === "object" &&
                typeof link.url === "string" &&
                link.url.trim() !== ""
        )
        .map(
            link => ({
                ...link,
                type:
                    getGameLinkType(
                        link
                    )
            })
        );
}


function getNormalizedLinkType(link) {

    return getGameLinkType(
        link
    )
        .toLowerCase()
        .replace(/[_-]+/g, " ")
        .trim();
}


function renderGameLinkList(
    links,
    container,
    mode = "default"
) {

    container.innerHTML = "";

    links.forEach(
        linkData => {

            const link =
                document.createElement(
                    "a"
                );

            link.className =
                "game-link";

            if (
                mode === "other"
            ) {

                link.classList.add(
                    "game-link-other"
                );

            }

            if (
                linkData.recommended === true
            ) {

                link.classList.add(
                    "recommended"
                );

            }

            link.href =
                linkData.url;

            link.target =
                "_blank";

            link.rel =
                "noopener noreferrer";

            const type =
                document.createElement(
                    "span"
                );

            type.className =
                "game-link-type";

            type.textContent =
                getGameLinkType(
                    linkData
                );

            const name =
                document.createElement(
                    "span"
                );

            name.className =
                "game-link-name";

            name.textContent =
                getGameLinkLabel(
                    linkData
                );

            const meta =
                document.createElement(
                    "span"
                );

            meta.className =
                "game-link-meta";

            const metaParts = [];

            if (
                linkData.region
            ) {

                metaParts.push(
                    linkData.region
                );

            }

            if (
                linkData.title_id
            ) {

                metaParts.push(
                    linkData.title_id
                );

            }

            if (
                linkData.version
            ) {

                metaParts.push(
                    "v" + String(linkData.version)
                );

            }

            if (
                linkData.required_fw
            ) {

                metaParts.push(
                    "FW " + String(linkData.required_fw)
                );

            }

            if (
                linkData.description
            ) {

                metaParts.push(
                    linkData.description
                );

            }

            meta.textContent =
                metaParts.join(
                    " · "
                );

            const size =
                document.createElement(
                    "span"
                );

            size.className =
                "game-link-size";

            if (
                typeof linkData.size ===
                    "number" &&
                linkData.size > 0
            ) {

                size.textContent =
                    formatFileSize(
                        linkData.size
                    );

            }

            if (
                linkData.recommended === true
            ) {

                const recommended =
                    document.createElement(
                        "span"
                    );

                recommended.className =
                    "game-link-recommended";

                recommended.textContent =
                    "Recommended";

                link.appendChild(
                    recommended
                );

            }

            link.appendChild(
                type
            );

            link.appendChild(
                name
            );

            if (
                meta.textContent
            ) {

                link.appendChild(
                    meta
                );

            }

            if (
                size.textContent
            ) {

                link.appendChild(
                    size
                );

            }

            container.appendChild(
                link
            );

        }
    );
}


function renderGameLinks(game) {

    const downloadsSection =
        document.getElementById(
            "game-downloads-section"
        );

    const downloadsContainer =
        document.getElementById(
            "game-downloads"
        );

    const dlcSection =
        document.getElementById(
            "game-dlc-section"
        );

    const dlcContainer =
        document.getElementById(
            "game-dlc"
        );

    const updatesSection =
        document.getElementById(
            "game-updates-section"
        );

    const updatesContainer =
        document.getElementById(
            "game-updates"
        );

    const otherLinksSection =
        document.getElementById(
            "game-other-links-section"
        );

    const otherLinksContainer =
        document.getElementById(
            "game-other-links"
        );

    downloadsContainer.innerHTML = "";
    dlcContainer.innerHTML = "";
    if (updatesContainer) {
        updatesContainer.innerHTML = "";
    }
    otherLinksContainer.innerHTML = "";

    downloadsSection.hidden = true;
    dlcSection.hidden = true;
    if (updatesSection) {
        updatesSection.hidden = true;
    }
    otherLinksSection.hidden = true;

    const links =
        normalizeGameLinks(
            game
        );

    if (
        links.length === 0
    ) {
        return;
    }

    const downloads = [];
    const dlcs = [];
    const updates = [];
    const otherLinks = [];

    links.forEach(
        link => {

            const type =
                getNormalizedLinkType(
                    link
                );

            if (
                type === "download" ||
                type === "downloads"
            ) {

                downloads.push(
                    link
                );

                return;
            }

            if (
                type === "dlc" ||
                type === "dlcs"
            ) {

                dlcs.push(
                    link
                );

                return;
            }

            if (
                type === "update" ||
                type === "updates" ||
                type === "patch" ||
                type === "patches"
            ) {

                updates.push(
                    link
                );

                return;
            }

            /*
             * Everything else goes here.
             *
             * Examples:
             * Repository
             * Official Website
             * Documentation
             * Issues
             * Community
             * Mirror
             * Other
             * and future types.
             */
            otherLinks.push(
                link
            );

        }
    );

    /*
     * Keep downloads ordered with
     * the recommended one first.
     */
    downloads.sort(
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

    // Prefer newer update versions first when version field exists
    updates.sort(
        (a, b) => {
            const va = String(a.version || "");
            const vb = String(b.version || "");
            if (va && vb && va !== vb) {
                return vb.localeCompare(va, undefined, { numeric: true });
            }
            if (a.recommended === true && b.recommended !== true) {
                return -1;
            }
            if (a.recommended !== true && b.recommended === true) {
                return 1;
            }
            return 0;
        }
    );

    renderGameLinkList(
        downloads,
        downloadsContainer,
        "download"
    );

    renderGameLinkList(
        dlcs,
        dlcContainer,
        "dlc"
    );

    if (updatesContainer) {
        renderGameLinkList(
            updates,
            updatesContainer,
            "update"
        );
    }

    renderGameLinkList(
        otherLinks,
        otherLinksContainer,
        "other"
    );

    downloadsSection.hidden =
        downloads.length === 0;

    dlcSection.hidden =
        dlcs.length === 0;

    if (updatesSection) {
        updatesSection.hidden =
            updates.length === 0;
    }

    otherLinksSection.hidden =
        otherLinks.length === 0;
}


/* ========================================
   Render game
======================================== */

function renderGame(game) {

    document.title =
        `${game.name} — PSVitaAlive Store`;

    document.getElementById(
        "game-name"
    ).textContent =
        gameDisplayValue(
            game.name,
            "Unknown game"
        );

    const cover =
        document.getElementById(
            "game-cover"
        );

    if (game.cover) {

        cover.src =
            game.cover;

        cover.alt =
            `${game.name} cover`;
    }

    document.getElementById(
        "game-description"
    ).textContent =
        gameDisplayValue(
            game.description
        );

    document.getElementById(
        "game-long-description"
    ).textContent =
        gameDisplayValue(
            game.long_description
        );

    const meta =
        document.getElementById(
            "game-meta"
        );

    meta.innerHTML = "";

    const metaValues = [];

    if (game.version) {

        metaValues.push(
            `Version ${game.version}`
        );
    }

    if (game.version_date) {

        metaValues.push(
            gameReleaseDate(
                game.version_date
            )
        );
    }

    if (
        typeof game.size ===
            "number" &&
        game.size > 0
    ) {

        metaValues.push(
            formatFileSize(
                game.size
            )
        );
    }

    meta.textContent =
        metaValues.join(
            " · "
        );

    renderGameCategories(
        game
    );

    renderGameTitleIds(
        game
    );

    document.getElementById(
        "info-version"
    ).textContent =
        gameDisplayValue(
            game.version
        );

    document.getElementById(
        "info-date"
    ).textContent =
        gameReleaseDate(
            game.version_date
        );

    document.getElementById(
        "info-size"
    ).textContent =
        (
            typeof game.size ===
                "number" &&
            game.size > 0
        )
            ? formatFileSize(
                game.size
            )
            : "Not available";

    renderGameLinks(
        game
    );

    document.getElementById(
        "game-loading"
    ).hidden = true;

    document.getElementById(
        "game-content"
    ).hidden = false;
}


/* ========================================
   Error
======================================== */

function showGameError() {

    document.getElementById(
        "game-loading"
    ).hidden = true;

    document.getElementById(
        "game-error"
    ).hidden = false;
}


/* ========================================
   Initialization
======================================== */

async function initGamePage() {

    try {

        const gameId =
            getGameIdFromUrl();

        if (!gameId) {

            showGameError();

            return;
        }

        const games =
            await loadOfficialGamesCatalog();

        const game =
            games.find(
                item =>
                    item.id === gameId
            );

        if (!game) {

            console.error(
                "Game not found:",
                gameId
            );

            showGameError();

            return;
        }

        renderGame(
            game
        );

        console.log(
            "Game page loaded:",
            game.name
        );

    } catch (error) {

        console.error(
            "Failed to load game:",
            error
        );

        showGameError();
    }
}


/* ========================================
   Start
======================================== */

document.addEventListener(
    "DOMContentLoaded",
    initGamePage
);
