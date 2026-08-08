/* ========================================
   PSVitaAlive Multi-Catalog Switcher
======================================== */

const PSVITA_ALIVE_GAME_CATALOGS = {
    psvita: {
        file: "catalog_psvita_games.json",
        label: "PS Vita Games",
        badge: "PS Vita Game"
    },

    psp: {
        file: "catalog_psp_games.json",
        label: "PSP Games",
        badge: "PSP Game"
    },

    ps1: {
        file: "catalog_ps1_games.json",
        label: "PS1 Games",
        badge: "PS1 Game"
    }
};

const PSVITA_ALIVE_GAME_CACHE = {};


/* ========================================
   Retry helper
======================================== */

async function loadGameCatalogWithRetry(
    catalog,
    attempts = 3
) {

    const config =
        PSVITA_ALIVE_GAME_CATALOGS[catalog];

    if (!config) {
        throw new Error(
            `Unknown game catalog: ${catalog}`
        );
    }

    if (
        PSVITA_ALIVE_GAME_CACHE[catalog]
    ) {
        return PSVITA_ALIVE_GAME_CACHE[catalog];
    }

    let lastError = null;

    for (
        let attempt = 1;
        attempt <= attempts;
        attempt++
    ) {

        try {

            if (
                window.showPSVitaAliveLoader
            ) {

                window.showPSVitaAliveLoader(
                    `Loading ${config.label}...`
                );

                if (
                    window.updatePSVitaAliveLoaderStatus
                ) {

                    window.updatePSVitaAliveLoaderStatus(
                        attempt === 1
                            ? "Downloading catalog..."
                            : `Retrying connection (${attempt}/${attempts})...`
                    );

                }

            }

            /*
             * loadJSON is provided by data.js and enhanced
             * by catalog-loader.js. It therefore keeps the
             * real progress bar.
             */
            const games =
                await loadJSON(
                    `${VITAHUB_RAW_BASE}/${config.file}`
                );

            if (
                !Array.isArray(games)
            ) {

                throw new Error(
                    `${config.file} did not return an array`
                );

            }

            PSVITA_ALIVE_GAME_CACHE[catalog] =
                games;

            return games;

        } catch (error) {

            lastError = error;

            console.error(
                `Failed loading ${config.label} (attempt ${attempt}/${attempts}):`,
                error
            );

            if (
                attempt < attempts
            ) {

                await new Promise(
                    resolve =>
                        setTimeout(
                            resolve,
                            500 * attempt
                        )
                );

            }

        }

    }

    throw lastError ||
        new Error(
            `Failed to load ${config.label} catalog.`
        );

}


function getPSVitaAliveCatalogConfig(
    catalog
) {

    return (
        PSVITA_ALIVE_GAME_CATALOGS[catalog] ||
        PSVITA_ALIVE_GAME_CATALOGS.psvita
    );

}


/* ========================================
   UI
======================================== */

function updatePSVitaAliveCatalogUI() {

    const buttons = [
        ["catalog-homebrew", "homebrew"],
        ["catalog-games", "psvita"],
        ["catalog-psp", "psp"],
        ["catalog-ps1", "ps1"]
    ];


    buttons.forEach(
        ([id, catalog]) => {

            const button =
                document.getElementById(id);

            if (button) {

                button.classList.toggle(
                    "active",
                    currentCatalog === catalog
                );

            }

        }
    );


    const title =
        document.getElementById(
            "catalog-title"
        );

    const description =
        document.getElementById(
            "catalog-description"
        );


    if (
        currentCatalog === "homebrew"
    ) {

        if (title) {
            title.textContent =
                "Latest Releases & Updates";
        }

        if (description) {
            description.textContent =
                "The latest Homebrew releases and updates.";
        }

        return;
    }


    const config =
        getPSVitaAliveCatalogConfig(
            currentCatalog
        );


    if (title) {
        title.textContent =
            config.label;
    }

    if (description) {
        description.textContent =
            `Explore the ${config.label} catalog.`;
    }

}


function updatePSVitaAliveRandomButton() {

    const button =
        document.getElementById(
            "random-homebrew"
        );

    if (!button) {
        return;
    }


    const disabled =
        currentCatalog !== "homebrew";


    button.disabled =
        disabled;

    button.classList.toggle(
        "disabled",
        disabled
    );

}


/* ========================================
   Cards
======================================== */

function renderPSVitaAliveGameCard(
    game,
    catalog
) {

    const card =
        renderOfficialGameCard(
            game
        );

    const config =
        getPSVitaAliveCatalogConfig(
            catalog
        );


    const badge =
        card.querySelector(
            ".official-game-badge"
        );


    if (badge) {
        badge.textContent =
            config.badge;
    }


    card.addEventListener(
        "click",
        event => {

            event.preventDefault();
            event.stopImmediatePropagation();


            window.location.href =
                `game.html?catalog=${encodeURIComponent(
                    catalog
                )}&id=${encodeURIComponent(
                    game.id
                )}`;

        },
        true
    );


    return card;

}


/* ========================================
   Render game catalog
======================================== */

async function renderPSVitaAliveGameCatalog(
    catalog
) {

    const grid =
        document.getElementById(
            "latest-apps-grid"
        );

    if (!grid) {
        return;
    }


    const config =
        getPSVitaAliveCatalogConfig(
            catalog
        );


    try {

        if (
            window.showPSVitaAliveLoader
        ) {

            window.showPSVitaAliveLoader(
                `Loading ${config.label}...`
            );

        }


        const games =
            await loadGameCatalogWithRetry(
                catalog
            );


        if (
            window.updatePSVitaAliveLoaderStatus
        ) {

            window.updatePSVitaAliveLoaderStatus(
                "Processing catalog..."
            );

        }


        grid.innerHTML = "";


        games.forEach(
            game => {

                grid.appendChild(
                    renderPSVitaAliveGameCard(
                        game,
                        catalog
                    )
                );

            }
        );


        if (
            window.hidePSVitaAliveLoader
        ) {

            window.hidePSVitaAliveLoader();

        }


    } catch (error) {

        console.error(
            `Failed to load ${config.label} catalog:`,
            error
        );


        /*
         * Keep the loader visible until the final retry
         * has really failed, then replace it with the
         * real error.
         */

        if (
            window.hidePSVitaAliveLoader
        ) {

            window.hidePSVitaAliveLoader();

        }


        grid.innerHTML = `
            <div class="catalog-error">
                <h3>Unable to load ${config.label}</h3>
                <p>
                    The catalog could not be loaded right now.
                    Please try again.
                </p>
            </div>
        `;

    }

}


/* ========================================
   Search
======================================== */

function renderPSVitaAliveGameSearchResults(
    results,
    query,
    catalog
) {

    const grid =
        document.getElementById(
            "latest-apps-grid"
        );

    if (!grid) {
        return;
    }


    grid.innerHTML = "";


    results.forEach(
        game => {

            grid.appendChild(
                renderPSVitaAliveGameCard(
                    game,
                    catalog
                )
            );

        }
    );


    if (
        results.length === 0
    ) {

        const config =
            getPSVitaAliveCatalogConfig(
                catalog
            );


        grid.innerHTML = `
            <div class="catalog-empty">
                <h3>No ${config.label} found</h3>
                <p>
                    No ${config.label} matched
                    "${escapeSearchText(query)}".
                </p>
            </div>
        `;

    }

}


/* ========================================
   Remove old app.js listeners
======================================== */

function replaceCatalogButtonListeners() {

    const ids = [
        "catalog-homebrew",
        "catalog-games",
        "catalog-psp",
        "catalog-ps1"
    ];


    ids.forEach(
        id => {

            const button =
                document.getElementById(id);

            if (!button) {
                return;
            }


            const clone =
                button.cloneNode(true);


            button.replaceWith(
                clone
            );

        }
    );

}


function replaceCatalogSearchListener() {

    const input =
        document.getElementById(
            "global-search"
        );

    if (!input) {
        return null;
    }


    const clone =
        input.cloneNode(true);


    input.replaceWith(
        clone
    );


    return clone;

}


/* ========================================
   Catalog switch
======================================== */

window.switchCatalog =
    async function(catalog) {

        currentCatalog =
            catalog;


        updatePSVitaAliveCatalogUI();
        updatePSVitaAliveRandomButton();


        const searchInput =
            document.getElementById(
                "global-search"
            );


        if (searchInput) {

            searchInput.value = "";


            searchInput.placeholder =
                currentCatalog === "homebrew"
                    ? "Search Homebrew..."
                    : `Search ${
                        getPSVitaAliveCatalogConfig(
                            currentCatalog
                        ).label
                    }...`;

        }


        if (
            currentCatalog === "homebrew"
        ) {

            if (
                window.hidePSVitaAliveLoader
            ) {

                window.hidePSVitaAliveLoader();

            }


            renderHomebrewCatalog();

        } else {

            await renderPSVitaAliveGameCatalog(
                currentCatalog
            );

        }

    };


window.performSearch =
    async function(query) {

        const normalizedQuery =
            normalizeSearchValue(
                query
            );


        if (!normalizedQuery) {

            await window.switchCatalog(
                currentCatalog
            );

            return;

        }


        if (
            currentCatalog === "homebrew"
        ) {

            const results =
                VitaHubData.catalog.filter(
                    app =>
                        normalizeSearchValue(
                            getHomebrewSearchText(
                                app
                            )
                        ).includes(
                            normalizedQuery
                        )
                );


            renderHomebrewSearchResults(
                results,
                query
            );

            return;

        }


        try {

            const games =
                await loadGameCatalogWithRetry(
                    currentCatalog
                );


            const results =
                games.filter(
                    game =>
                        normalizeSearchValue(
                            getGameSearchText(
                                game
                            )
                        ).includes(
                            normalizedQuery
                        )
                );


            renderPSVitaAliveGameSearchResults(
                results,
                query,
                currentCatalog
            );

        } catch (error) {

            console.error(
                "Failed to search selected game catalog:",
                error
            );

        }

    };


/* ========================================
   Initialize
======================================== */

function initPSVitaAliveCatalogSwitcher() {

    replaceCatalogButtonListeners();


    const searchInput =
        replaceCatalogSearchListener();


    const buttons = [
        ["catalog-homebrew", "homebrew"],
        ["catalog-games", "psvita"],
        ["catalog-psp", "psp"],
        ["catalog-ps1", "ps1"]
    ];


    buttons.forEach(
        ([id, catalog]) => {

            const button =
                document.getElementById(id);

            if (!button) {
                return;
            }


            button.addEventListener(
                "click",
                () => {

                    window.switchCatalog(
                        catalog
                    );

                }
            );

        }
    );


    if (searchInput) {

        let searchTimeout;


        searchInput.addEventListener(
            "input",
            () => {

                clearTimeout(
                    searchTimeout
                );


                searchTimeout =
                    setTimeout(
                        () => {

                            window.performSearch(
                                searchInput.value
                            );

                        },
                        180
                    );

            }
        );

    }


    updatePSVitaAliveCatalogUI();
    updatePSVitaAliveRandomButton();

}


/* ========================================
   Start
======================================== */

document.addEventListener(
    "DOMContentLoaded",
    initPSVitaAliveCatalogSwitcher
);
