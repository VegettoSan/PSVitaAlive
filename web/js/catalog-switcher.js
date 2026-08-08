/* PSVitaAlive multi-catalog switcher */

const PSVITA_ALIVE_GAME_CATALOGS = {
    psvita: { file: "catalog_psvita_games.json", label: "PS Vita Games", badge: "PS Vita Game" },
    psp:    { file: "catalog_psp_games.json",    label: "PSP Games",     badge: "PSP Game" },
    ps1:    { file: "catalog_ps1_games.json",    label: "PS1 Games",     badge: "PS1 Game" }
};

const PSVITA_ALIVE_GAME_CACHE = {};

async function loadPSVitaAliveGameCatalog(catalog) {
    const config = PSVITA_ALIVE_GAME_CATALOGS[catalog];
    if (!config) throw new Error(`Unknown game catalog: ${catalog}`);
    if (PSVITA_ALIVE_GAME_CACHE[catalog]) return PSVITA_ALIVE_GAME_CACHE[catalog];

    const games = await loadJSON(`${VITAHUB_RAW_BASE}/${config.file}`);
    PSVITA_ALIVE_GAME_CACHE[catalog] = games;
    return games;
}

function getPSVitaAliveCatalogConfig(catalog) {
    return PSVITA_ALIVE_GAME_CATALOGS[catalog] || PSVITA_ALIVE_GAME_CATALOGS.psvita;
}

function updatePSVitaAliveCatalogUI() {
    const buttons = [
        ["catalog-homebrew", "homebrew"],
        ["catalog-games", "psvita"],
        ["catalog-psp", "psp"],
        ["catalog-ps1", "ps1"]
    ];

    buttons.forEach(([id, catalog]) => {
        const button = document.getElementById(id);
        if (button) button.classList.toggle("active", currentCatalog === catalog);
    });

    const title = document.getElementById("catalog-title");
    const description = document.getElementById("catalog-description");

    if (currentCatalog === "homebrew") {
        if (title) title.textContent = "Latest Releases & Updates";
        if (description) description.textContent = "The latest Homebrew releases and updates.";
        return;
    }

    const config = getPSVitaAliveCatalogConfig(currentCatalog);
    if (title) title.textContent = config.label;
    if (description) description.textContent = `Explore the ${config.label} catalog.`;
}

function updatePSVitaAliveRandomButton() {
    const button = document.getElementById("random-homebrew");
    if (!button) return;

    const disabled = currentCatalog !== "homebrew";
    button.disabled = disabled;
    button.classList.toggle("disabled", disabled);
}

function renderPSVitaAliveGameCard(game, catalog) {
    const card = renderOfficialGameCard(game);
    const config = getPSVitaAliveCatalogConfig(catalog);
    const badge = card.querySelector(".official-game-badge");

    if (badge) badge.textContent = config.badge;

    // Override components.js navigation with the selected catalog.
    card.addEventListener("click", event => {
        event.preventDefault();
        event.stopImmediatePropagation();
        window.location.href =
            `game.html?catalog=${encodeURIComponent(catalog)}&id=${encodeURIComponent(game.id)}`;
    }, true);

    return card;
}

async function renderPSVitaAliveGameCatalog(catalog) {
    const grid = document.getElementById("latest-apps-grid");
    if (!grid) return;

    const config = getPSVitaAliveCatalogConfig(catalog);

    try {
        const games = await loadPSVitaAliveGameCatalog(catalog);
        grid.innerHTML = "";

        games.forEach(game => {
            grid.appendChild(renderPSVitaAliveGameCard(game, catalog));
        });
    } catch (error) {
        console.error(`Failed to load ${config.label} catalog:`, error);
        grid.innerHTML = `<p class="catalog-error">Failed to load ${config.label} catalog.</p>`;
    }
}

function renderPSVitaAliveGameSearchResults(results, query, catalog) {
    const grid = document.getElementById("latest-apps-grid");
    if (!grid) return;

    grid.innerHTML = "";

    results.forEach(game => {
        grid.appendChild(renderPSVitaAliveGameCard(game, catalog));
    });

    if (results.length === 0) {
        const config = getPSVitaAliveCatalogConfig(catalog);
        grid.innerHTML = `
            <div class="catalog-empty">
                <h3>No ${config.label} found</h3>
                <p>No ${config.label} matched "${escapeSearchText(query)}".</p>
            </div>
        `;
    }
}

// Replace the original two-catalog switcher with the four-catalog version.
window.switchCatalog = async function(catalog) {
    currentCatalog = catalog;
    updatePSVitaAliveCatalogUI();
    updatePSVitaAliveRandomButton();

    const searchInput = document.getElementById("global-search");
    if (searchInput) {
        searchInput.value = "";
        searchInput.placeholder = currentCatalog === "homebrew"
            ? "Search Homebrew..."
            : `Search ${getPSVitaAliveCatalogConfig(currentCatalog).label}...`;
    }

    if (currentCatalog === "homebrew") {
        renderHomebrewCatalog();
    } else {
        await renderPSVitaAliveGameCatalog(currentCatalog);
    }
};

// Search only inside the currently selected catalog.
window.performSearch = async function(query) {
    const normalizedQuery = normalizeSearchValue(query);

    if (!normalizedQuery) {
        await window.switchCatalog(currentCatalog);
        return;
    }

    if (currentCatalog === "homebrew") {
        const results = VitaHubData.catalog.filter(app =>
            normalizeSearchValue(getHomebrewSearchText(app)).includes(normalizedQuery)
        );

        renderHomebrewSearchResults(results, query);
        return;
    }

    try {
        const games = await loadPSVitaAliveGameCatalog(currentCatalog);
        const results = games.filter(game =>
            normalizeSearchValue(getGameSearchText(game)).includes(normalizedQuery)
        );

        renderPSVitaAliveGameSearchResults(results, query, currentCatalog);
    } catch (error) {
        console.error("Failed to search selected game catalog:", error);
    }
};

function initPSVitaAliveCatalogSwitcher() {
    const buttons = [
        ["catalog-homebrew", "homebrew"],
        ["catalog-games", "psvita"],
        ["catalog-psp", "psp"],
        ["catalog-ps1", "ps1"]
    ];

    buttons.forEach(([id, catalog]) => {
        const button = document.getElementById(id);
        if (button) button.onclick = () => window.switchCatalog(catalog);
    });

    updatePSVitaAliveCatalogUI();
    updatePSVitaAliveRandomButton();
    window.switchCatalog("homebrew");
}

document.addEventListener("DOMContentLoaded", initPSVitaAliveCatalogSwitcher);
