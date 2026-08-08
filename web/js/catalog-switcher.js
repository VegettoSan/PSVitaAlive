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
   Direct game catalog loader
   This deliberately does NOT use the global
   loadJSON() replacement from catalog-loader.js.
   That prevents PS Vita from competing with
   the old loader lifecycle.
======================================== */

async function fetchGameCatalogWithProgress(
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

    if (PSVITA_ALIVE_GAME_CACHE[catalog]) {
        return PSVITA_ALIVE_GAME_CACHE[catalog];
    }

    let lastError = null;

    for (let attempt = 1; attempt <= attempts; attempt++) {
        try {
            if (window.showPSVitaAliveLoader) {
                window.showPSVitaAliveLoader(
                    `Loading ${config.label}...`
                );
            }

            if (window.updatePSVitaAliveLoaderStatus) {
                window.updatePSVitaAliveLoaderStatus(
                    attempt === 1
                        ? "Connecting to catalog..."
                        : `Retrying connection (${attempt}/${attempts})...`
                );
            }

            const url =
                `${VITAHUB_RAW_BASE}/${config.file}?v=${Date.now()}-${attempt}`;

            const response =
                await fetch(
                    url,
                    {
                        cache: "no-store"
                    }
                );

            if (!response.ok) {
                throw new Error(
                    `${response.status} ${response.statusText}`
                );
            }

            if (!response.body) {
                const games = await response.json();

                if (!Array.isArray(games)) {
                    throw new Error(
                        `${config.file} did not return an array`
                    );
                }

                PSVITA_ALIVE_GAME_CACHE[catalog] = games;
                return games;
            }

            const total =
                Number(
                    response.headers.get(
                        "content-length"
                    )
                );

            const reader =
                response.body.getReader();

            const chunks = [];
            let received = 0;

            while (true) {
                const {
                    done,
                    value
                } = await reader.read();

                if (done) {
                    break;
                }

                chunks.push(value);
                received += value.byteLength;

                if (
                    Number.isFinite(total) &&
                    total > 0
                ) {
                    const progress =
                        Math.min(
                            99,
                            Math.round(
                                received / total * 100
                            )
                        );

                    if (
                        window.setPSVitaAliveLoaderProgress
                    ) {
                        window.setPSVitaAliveLoaderProgress(
                            progress
                        );
                    }
                }
            }

            if (window.updatePSVitaAliveLoaderStatus) {
                window.updatePSVitaAliveLoaderStatus(
                    "Processing catalog..."
                );
            }

            const combined =
                new Uint8Array(received);

            let offset = 0;

            for (const chunk of chunks) {
                combined.set(
                    chunk,
                    offset
                );

                offset += chunk.length;
            }

            const text =
                new TextDecoder(
                    "utf-8"
                ).decode(
                    combined
                );

            const games =
                JSON.parse(text);

            if (!Array.isArray(games)) {
                throw new Error(
                    `${config.file} did not return an array`
                );
            }

            if (window.setPSVitaAliveLoaderProgress) {
                window.setPSVitaAliveLoaderProgress(100);
            }

            PSVITA_ALIVE_GAME_CACHE[catalog] =
                games;

            return games;

        } catch (error) {
            lastError = error;

            console.warn(
                `Catalog load failed: ${config.label} (${attempt}/${attempts})`,
                error
            );

            if (attempt < attempts) {
                await new Promise(
                    resolve =>
                        setTimeout(
                            resolve,
                            700 * attempt
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


/* ========================================
   UI
======================================== */

function getPSVitaAliveCatalogConfig(
    catalog
) {
    return (
        PSVITA_ALIVE_GAME_CATALOGS[catalog] ||
        PSVITA_ALIVE_GAME_CATALOGS.psvita
    );
}


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

    if (currentCatalog === "homebrew") {
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
   Render catalog
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
        if (window.showPSVitaAliveLoader) {
            window.showPSVitaAliveLoader(
                `Loading ${config.label}...`
            );
        }

        const games =
            await fetchGameCatalogWithProgress(
                catalog
            );

        if (window.updatePSVitaAliveLoaderStatus) {
            window.updatePSVitaAliveLoaderStatus(
                "Preparing catalog..."
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

        if (window.setPSVitaAliveLoaderProgress) {
            window.setPSVitaAliveLoaderProgress(
                100
            );
        }

        if (window.updatePSVitaAliveLoaderStatus) {
            window.updatePSVitaAliveLoaderStatus(
                "Ready"
            );
        }

        setTimeout(
            () => {
                if (window.hidePSVitaAliveLoader) {
                    window.hidePSVitaAliveLoader();
                }
            },
            180
        );

    } catch (error) {
        console.error(
            `Failed to load ${config.label} catalog:`,
            error
        );

        if (window.hidePSVitaAliveLoader) {
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

    if (results.length === 0) {
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
   Remove old listeners
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
            if (window.hidePSVitaAliveLoader) {
                window.hidePSVitaAliveLoader();
            }

            renderHomebrewCatalog();
            return;
        }

        await renderPSVitaAliveGameCatalog(
            currentCatalog
        );
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
            if (window.showPSVitaAliveLoader) {
                window.showPSVitaAliveLoader(
                    `Searching ${getPSVitaAliveCatalogConfig(
                        currentCatalog
                    ).label}...`
                );
            }

            const games =
                await fetchGameCatalogWithProgress(
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

            if (window.hidePSVitaAliveLoader) {
                window.hidePSVitaAliveLoader();
            }

        } catch (error) {
            console.error(
                "Failed to search selected game catalog:",
                error
            );

            if (window.hidePSVitaAliveLoader) {
                window.hidePSVitaAliveLoader();
            }
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


document.addEventListener(
    "DOMContentLoaded",
    initPSVitaAliveCatalogSwitcher
);
