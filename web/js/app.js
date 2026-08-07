const HOME_CATALOG = "homebrew";
const GAMES_CATALOG = "games";

let currentCatalog =
    HOME_CATALOG;

    /* ========================================
   Render Featured Homebrew
======================================== */

function renderFeaturedHomebrew() {

    const featuredGrid =
        document.getElementById(
            "featured-homebrew-grid"
        );


    if (!featuredGrid) {
        return;
    }


    featuredGrid.innerHTML = "";


    const sortedApps =
        [...VitaHubData.catalog].sort(
            (a, b) => {

                const dateA =
                    new Date(
                        a.version_date || 0
                    ).getTime();


                const dateB =
                    new Date(
                        b.version_date || 0
                    ).getTime();


                const validA =
                    Number.isFinite(
                        dateA
                    ) && dateA > 0;


                const validB =
                    Number.isFinite(
                        dateB
                    ) && dateB > 0;


                if (
                    !validA &&
                    validB
                ) {
                    return 1;
                }


                if (
                    validA &&
                    !validB
                ) {
                    return -1;
                }


                if (
                    !validA &&
                    !validB
                ) {
                    return 0;
                }


                return dateB - dateA;

            }
        );


    const featuredApps =
        sortedApps.slice(
            0,
            3
        );


    featuredApps.forEach(
        app => {

            const card =
                renderAppCard(
                    app
                );


            card.classList.add(
                "featured-app-card"
            );


            featuredGrid.appendChild(
                card
            );

        }
    );

}

/* ========================================
   Render Homebrew
======================================== */

function renderHomebrewCatalog() {

    const appsGrid =
        document.getElementById(
            "latest-apps-grid"
        );

    if (!appsGrid) {
        return;
    }


    appsGrid.innerHTML = "";


    const sortedApps =
    [...VitaHubData.catalog].sort(
        (a, b) => {

            const dateA =
                new Date(
                    a.version_date || 0
                ).getTime();

            const dateB =
                new Date(
                    b.version_date || 0
                ).getTime();


            const validA =
                Number.isFinite(
                    dateA
                ) && dateA > 0;


            const validB =
                Number.isFinite(
                    dateB
                ) && dateB > 0;


            /*
             * Applications without a valid
             * date go to the end.
             */

            if (
                !validA &&
                validB
            ) {
                return 1;
            }


            if (
                validA &&
                !validB
            ) {
                return -1;
            }


            if (
                !validA &&
                !validB
            ) {
                return 0;
            }


            /*
             * Newest first.
             */

            return dateB - dateA;

        }
    );


sortedApps.forEach(
    app => {

        const card =
            renderAppCard(
                app
            );

        appsGrid.appendChild(
            card
        );

    }
);

}


/* ========================================
   Render Official Games
======================================== */

async function renderOfficialGamesCatalog() {

    const appsGrid =
        document.getElementById(
            "latest-apps-grid"
        );

    if (!appsGrid) {
        return;
    }


    try {

        const games =
            await loadOfficialGames();


        appsGrid.innerHTML = "";


        games.forEach(
            game => {

                const card =
                    renderOfficialGameCard(
                        game
                    );

                appsGrid.appendChild(
                    card
                );

            }
        );

    } catch (error) {

        console.error(
            "Failed to load official PS Vita games:",
            error
        );


        appsGrid.innerHTML = `
            <p class="catalog-error">
                Failed to load PS Vita games catalog.
            </p>
        `;

    }

}

/* ========================================
   Search
======================================== */

function normalizeSearchValue(value) {

    return String(value || "")
        .toLowerCase()
        .normalize("NFD")
        .replace(/[\u0300-\u036f]/g, "")
        .trim();

}


/**
 * Texto utilizado para buscar Homebrew.
 */
function getHomebrewSearchText(app) {

    const authors =
        getAuthorsByIds(
            app.author_ids
        );


    return [
        app.id,
        app.title_id,
        app.name,
        app.description,
        app.long_description,
        app.category_id,
        ...(Array.isArray(app.subcategory_ids)
            ? app.subcategory_ids
            : []),
        ...authors.map(
            author => author.name
        )
    ]
        .filter(Boolean)
        .join(" ");

}


/**
 * Texto utilizado para buscar
 * juegos oficiales.
 */
function getGameSearchText(game) {

    const titleIds =
        game.title_ids &&
        typeof game.title_ids === "object"
            ? Object.values(
                game.title_ids
            )
            : [];


    return [
        game.id,
        game.name,
        game.description,
        game.long_description,
        game.category_id,
        ...(Array.isArray(game.subcategory_ids)
            ? game.subcategory_ids
            : []),
        ...titleIds
    ]
        .filter(Boolean)
        .join(" ");

}


/**
 * Muestra resultados de búsqueda
 * exclusivamente del catálogo Homebrew.
 */
function renderHomebrewSearchResults(
    results,
    query
) {

    const appsGrid =
        document.getElementById(
            "latest-apps-grid"
        );


    if (!appsGrid) {
        return;
    }


    appsGrid.innerHTML = "";


    const sortedResults =
        [...results].sort(
            (a, b) => {

                const dateA =
                    new Date(
                        a.version_date || 0
                    ).getTime();


                const dateB =
                    new Date(
                        b.version_date || 0
                    ).getTime();


                return dateB - dateA;

            }
        );


    sortedResults.forEach(
        app => {

            appsGrid.appendChild(
                renderAppCard(app)
            );

        }
    );


    if (
        sortedResults.length === 0
    ) {

        appsGrid.innerHTML = `
            <div class="catalog-empty">
                <h3>No Homebrew found</h3>

                <p>
                    No Homebrew matched
                    "${escapeSearchText(query)}".
                </p>
            </div>
        `;

    }

}


/**
 * Muestra resultados de búsqueda
 * exclusivamente del catálogo
 * oficial de juegos.
 */
function renderGameSearchResults(
    results,
    query
) {

    const appsGrid =
        document.getElementById(
            "latest-apps-grid"
        );


    if (!appsGrid) {
        return;
    }


    appsGrid.innerHTML = "";


    results.forEach(
        game => {

            appsGrid.appendChild(
                renderOfficialGameCard(game)
            );

        }
    );


    if (
        results.length === 0
    ) {

        appsGrid.innerHTML = `
            <div class="catalog-empty">
                <h3>No PS Vita games found</h3>

                <p>
                    No PS Vita game matched
                    "${escapeSearchText(query)}".
                </p>
            </div>
        `;

    }

}


/**
 * Evita insertar directamente
 * el texto escrito por el usuario
 * dentro del HTML.
 */
function escapeSearchText(value) {

    return String(value || "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");

}


/**
 * Ejecuta la búsqueda únicamente
 * sobre el catálogo seleccionado.
 */
async function performSearch(query) {

    const normalizedQuery =
        normalizeSearchValue(
            query
        );


    /*
     * Si el campo queda vacío,
     * restauramos el catálogo actual.
     */

    if (!normalizedQuery) {

        await switchCatalog(
            currentCatalog
        );

        return;

    }


    /*
     * HOMEbrew
     */

    if (
        currentCatalog ===
        HOME_CATALOG
    ) {

        const results =
            VitaHubData.catalog.filter(
                app => {

                    const text =
                        normalizeSearchValue(
                            getHomebrewSearchText(
                                app
                            )
                        );


                    return text.includes(
                        normalizedQuery
                    );

                }
            );


        renderHomebrewSearchResults(
            results,
            query
        );


        return;

    }


    /*
     * PS Vita Games
     */

    try {

        const games =
            await loadOfficialGames();


        const results =
            games.filter(
                game => {

                    const text =
                        normalizeSearchValue(
                            getGameSearchText(
                                game
                            )
                        );


                    return text.includes(
                        normalizedQuery
                    );

                }
            );


        renderGameSearchResults(
            results,
            query
        );


    } catch (error) {

        console.error(
            "Failed to search PS Vita games:",
            error
        );

    }

}

/* ========================================
   Catalog selector
======================================== */

function updateCatalogButtons() {

    const homeButton =
        document.getElementById(
            "catalog-homebrew"
        );

    const gamesButton =
        document.getElementById(
            "catalog-games"
        );


    if (!homeButton || !gamesButton) {
        return;
    }


    homeButton.classList.toggle(
        "active",
        currentCatalog === HOME_CATALOG
    );


    gamesButton.classList.toggle(
        "active",
        currentCatalog === GAMES_CATALOG
    );

}


async function switchCatalog(
    catalog
) {

    currentCatalog =
        catalog;


    updateCatalogButtons();
    updateRandomButton();


    /*
     * Limpiar búsqueda al cambiar
     * de catálogo.
     */

    const searchInput =
        document.getElementById(
            "global-search"
        );


    if (searchInput) {

        searchInput.value = "";

        searchInput.placeholder =
            currentCatalog === GAMES_CATALOG
                ? "Search PS Vita Games..."
                : "Search Homebrew...";

    }


    /*
     * Renderizar únicamente
     * el catálogo seleccionado.
     */

    if (
        currentCatalog ===
        GAMES_CATALOG
    ) {

        await renderOfficialGamesCatalog();

    } else {

        renderHomebrewCatalog();

    }

}


/* ========================================
   Initialize
======================================== */

async function initHomePage() {

    try {

        await loadVitaHubData();
        renderFeaturedHomebrew();

        const searchInput =
    document.getElementById(
        "global-search"
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

                        performSearch(
                            searchInput.value
                        );

                    },
                    180
                );

        }
    );

}

const randomButton =
    document.getElementById(
        "random-homebrew"
    );


if (randomButton) {

    randomButton.addEventListener(
        "click",
        openRandomHomebrew
    );

}


        const homebrewButton =
            document.getElementById(
                "catalog-homebrew"
            );


        const gamesButton =
            document.getElementById(
                "catalog-games"
            );


        if (
            homebrewButton &&
            gamesButton
        ) {

            homebrewButton.addEventListener(
                "click",
                () => {

                    switchCatalog(
                        HOME_CATALOG
                    );

                }
            );


            gamesButton.addEventListener(
                "click",
                () => {

                    switchCatalog(
                        GAMES_CATALOG
                    );

                }
            );

        }


        await switchCatalog(
            HOME_CATALOG
        );


    } catch (error) {

        console.error(
            "Failed to initialize PSVitaAlive Store:",
            error
        );

    }

}


document.addEventListener(
    "DOMContentLoaded",
    initHomePage
);

/* ========================================
   Random Homebrew
======================================== */

function openRandomHomebrew() {

    /*
     * Random siempre pertenece
     * exclusivamente al catálogo Homebrew.
     */

    if (
        currentCatalog !==
        HOME_CATALOG
    ) {

        return;

    }


    if (
        !Array.isArray(
            VitaHubData.catalog
        ) ||
        VitaHubData.catalog.length === 0
    ) {

        return;

    }


    const randomIndex =
        Math.floor(
            Math.random() *
            VitaHubData.catalog.length
        );


    const app =
        VitaHubData.catalog[
            randomIndex
        ];


    if (!app.title_id) {
        return;
    }


    window.location.href =
        `app.html?title_id=${encodeURIComponent(
            app.title_id
        )}`;

}

function updateRandomButton() {

    const randomButton =
        document.getElementById(
            "random-homebrew"
        );


    if (!randomButton) {
        return;
    }


    const disabled =
        currentCatalog ===
        GAMES_CATALOG;


    randomButton.disabled =
        disabled;


    randomButton.classList.toggle(
        "disabled",
        disabled
    );

}