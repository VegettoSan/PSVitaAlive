const HOME_CATALOG = "homebrew";
const GAMES_CATALOG = "games";

let currentCatalog =
    HOME_CATALOG;


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