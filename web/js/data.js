const VITAHUB_RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main";

const VitaHubData = {
    catalog: [],
    authors: [],
    categories: [],
    officialGames: [],

    loaded: false,
    officialGamesLoaded: false
};


/**
 * Always request the six generated public catalogs fresh. GitHub RAW and
 * browsers may cache URLs on the moving "main" ref after Actions publishes.
 * A per-request URL token avoids reusing an older CDN cache entry; no-store
 * avoids browser HTTP cache reuse. Leave media URLs untouched.
 */
let vitaHubCatalogRequestId = 0;
const VITAHUB_GENERATED_CATALOG = /\/(?:catalog|authors|categories|catalog_psvita_games|catalog_psp_games|catalog_ps1_games)\.json(?:[?#]|$)/;

function freshVitaHubCatalogUrl(path) {
    if (typeof path !== "string" ||
        !path.startsWith(`${VITAHUB_RAW_BASE}/`) ||
        !VITAHUB_GENERATED_CATALOG.test(path)) {
        return path;
    }
    const join = path.includes("?") ? "&" : "?";
    return `${path}${join}psva_refresh=${Date.now()}-${++vitaHubCatalogRequestId}`;
}

function fetchFreshVitaHubCatalog(path) {
    return fetch(freshVitaHubCatalogUrl(path), { cache: "no-store" });
}

/**
 * Carga un archivo JSON.
 */
async function loadJSON(path) {
    const response = await fetchFreshVitaHubCatalog(path);

    if (!response.ok) {
        throw new Error(
            `No se pudo cargar ${path}: ${response.status} ${response.statusText}`
        );
    }

    const data = await response.json();
    // Keep all current UI components on their V1 object contract.
    // Both V1 cached/public JSON and V2 compact JSON are accepted.
    const adapter = window.PSVitaAliveCatalogSchemaV2;
    return adapter ? adapter.toV1(data) : data;
}


/**
 * Carga los tres catálogos generados por VitaHub.
 */
async function loadVitaHubData() {
    if (VitaHubData.loaded) {
        return VitaHubData;
    }

    const [
        catalog,
        authors,
        categories
    ] = await Promise.all([
        loadJSON(`${VITAHUB_RAW_BASE}/catalog.json`),
        loadJSON(`${VITAHUB_RAW_BASE}/authors.json`),
        loadJSON(`${VITAHUB_RAW_BASE}/categories.json`)
    ]);

    VitaHubData.catalog = catalog;
    VitaHubData.authors = authors;
    VitaHubData.categories = categories;

    VitaHubData.loaded = true;

    return VitaHubData;
}

/**
 * Carga el catálogo de juegos oficiales de PS Vita.
 */
async function loadOfficialGames() {

    if (VitaHubData.officialGamesLoaded) {
        return VitaHubData.officialGames;
    }

    VitaHubData.officialGames = await loadJSON(
        `${VITAHUB_RAW_BASE}/catalog_psvita_games.json`
    );

    VitaHubData.officialGamesLoaded = true;

    return VitaHubData.officialGames;
}


/**
 * Busca un juego oficial utilizando su ID.
 */
function getOfficialGameById(id) {

    return VitaHubData.officialGames.find(
        game => game.id === id
    ) || null;
}


/**
 * Busca una aplicación utilizando su Title ID.
 */
function getAppByTitleId(titleId) {
    return VitaHubData.catalog.find(
        app => app.title_id === titleId
    ) || null;
}


/**
 * Busca un autor utilizando su ID.
 */
function getAuthorById(id) {
    return VitaHubData.authors.find(
        author => author.id === id
    ) || null;
}

function getAuthorsByIds(ids) {
    if (!Array.isArray(ids)) {
        return [];
    }

    return ids
        .map(id => getAuthorById(id))
        .filter(author => author);
}


/**
 * Busca una categoría utilizando su ID.
 */
function getCategoryById(id) {
    return VitaHubData.categories.find(
        category => category.id === id
    ) || null;
}