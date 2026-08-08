const HOME_CATALOG = "homebrew";

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
   Search helpers
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
                renderAppCard(
                    app
                )
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


/* ========================================
   Initialize Home Page
======================================== */

async function initHomePage() {

    try {

        await loadVitaHubData();

        renderFeaturedHomebrew();

        /*
         * The multi-catalog switcher owns:
         *
         * - Homebrew button
         * - PS Vita Games button
         * - PSP Games button
         * - PS1 Games button
         * - Global search
         *
         * app.js intentionally does NOT attach
         * listeners to those controls anymore.
         */

        renderHomebrewCatalog();

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
        currentCatalog !==
        HOME_CATALOG;

    randomButton.disabled =
        disabled;

    randomButton.classList.toggle(
        "disabled",
        disabled
    );
}
