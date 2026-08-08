/* PSVitaAlive shared game detail catalog selector */

const PSVITA_ALIVE_GAME_PAGE_CATALOGS = {
    psvita: {
        file: "catalog_psvita_games.json",
        label: "PS Vita Games",
        badge: "PS Vita Game",
        backLabel: "Back to PS Vita Games"
    },
    psp: {
        file: "catalog_psp_games.json",
        label: "PSP Games",
        badge: "PSP Game",
        backLabel: "Back to PSP Games"
    },
    ps1: {
        file: "catalog_ps1_games.json",
        label: "PS1 Games",
        badge: "PS1 Game",
        backLabel: "Back to PS1 Games"
    }
};

function getPSVitaAliveGamePageCatalog() {
    const params = new URLSearchParams(window.location.search);
    const catalog = params.get("catalog");
    return PSVITA_ALIVE_GAME_PAGE_CATALOGS[catalog] ? catalog : "psvita";
}

// game-page.js calls this during its existing initialization.
window.loadOfficialGamesCatalog = async function() {
    const catalog = getPSVitaAliveGamePageCatalog();
    const config = PSVITA_ALIVE_GAME_PAGE_CATALOGS[catalog];
    return loadJSON(`${VITAHUB_RAW_BASE}/${config.file}`);
};

function updatePSVitaAliveGamePageUI() {
    const catalog = getPSVitaAliveGamePageCatalog();
    const config = PSVITA_ALIVE_GAME_PAGE_CATALOGS[catalog];

    document.title = `${config.label} — PSVitaAlive Store`;

    const badge = document.querySelector(".game-catalog-badge");
    if (badge) badge.textContent = config.badge;

    const back = document.querySelector(".game-back-link");
    if (back) {
        back.textContent = `← ${config.backLabel}`;
        back.href = "index.html#homebrew-catalog";
    }

    const error = document.querySelector("#game-error p");
    if (error) {
        error.textContent =
            `The requested game could not be found in the ${config.label} catalog.`;
    }
}

document.addEventListener("DOMContentLoaded", updatePSVitaAliveGamePageUI);
