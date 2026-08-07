/**
 * PSVitaAlive Store
 *
 * Official PS Vita game detail page.
 *
 * Example:
 *
 * game.html?id=persona-4-golden
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

async function loadOfficialGamesCatalog() {

    return loadJSON(
        "../catalog_psvita_games.json"
    );

}


/* ========================================
   Categories
======================================== */

function renderGameCategories(
    game
) {

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

function renderGameTitleIds(
    game
) {

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


    if (entries.length === 0) {

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
                    
                item.appendChild(
                    regionElement
                );

                item.appendChild(
                    labelElement
                );

                item.appendChild(
                    valueElement
                );
    


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

function getGameLinkLabel(
    link
) {

    if (link.name) {
        return link.name;
    }

    if (link.type) {
        return link.type;
    }

    return "Open link";
}


function renderGameLinks(
    game
) {

    const section =
        document.getElementById(
            "game-links-section"
        );

    const container =
        document.getElementById(
            "game-links"
        );


    container.innerHTML = "";


    if (
        !Array.isArray(
            game.links
        ) ||
        game.links.length === 0
    ) {

        section.hidden = true;

        return;
    }


    section.hidden = false;


    const links = [
        ...game.links
    ];
    const downloads = [];

    const dlcs = [];

    const otherLinks = [];

    links.forEach(
    link => {

        const type =
            String(
                link.type || ""
            ).toLowerCase();


        const name =
            String(
                link.name || ""
            ).toLowerCase();


        if (
            type.includes("dlc") ||
            name.includes("dlc")
        ) {

            dlcs.push(
                link
            );

            return;

        }


        if (
            type.includes("download") ||
            type.includes("pkg")
        ) {

            downloads.push(
                link
            );

            return;

        }


        otherLinks.push(
            link
        );

    }
);

    /*
     * Recommended download first.
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

            if (!linkData.url) {
                return;
            }


            const link =
                document.createElement(
                    "a"
                );

            link.className =
                "game-link";


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


            if (linkData.type) {

                metaParts.push(
                    linkData.type
                );

            }


            if (linkData.region) {

                metaParts.push(
                    linkData.region
                );

            }


            if (linkData.title_id) {

                metaParts.push(
                    linkData.title_id
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


            link.appendChild(
                name
            );

            link.appendChild(
                meta
            );


            if (size.textContent) {

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


/* ========================================
   Render game
======================================== */

function renderGame(
    game
) {

    document.title =
        `${game.name} — PSVitaAlive Store`;


    /* Name */

    document.getElementById(
        "game-name"
    ).textContent =
        gameDisplayValue(
            game.name,
            "Unknown game"
        );


    /* Cover */

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


    /* Description */

    document.getElementById(
        "game-description"
    ).textContent =
        gameDisplayValue(
            game.description
        );


    /* Long description */

    document.getElementById(
        "game-long-description"
    ).textContent =
        gameDisplayValue(
            game.long_description
        );


    /* Meta */

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


    /* Categories */

    renderGameCategories(
        game
    );


    /* Title IDs */

    renderGameTitleIds(
        game
    );


    /* Information */

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


    /* Links */

    renderGameLinks(
        game
    );


    /* Show */

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
                "Official PS Vita game not found:",
                gameId
            );

            showGameError();

            return;
        }


        renderGame(
            game
        );


        console.log(
            "Official PS Vita game page loaded:",
            game.name
        );

    } catch (error) {

        console.error(
            "Failed to load official PS Vita game:",
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