const VitaHubData = {
    catalog: [],
    authors: [],
    categories: [],

    loaded: false
};


/**
 * Carga un archivo JSON.
 */
async function loadJSON(path) {
    const response = await fetch(path);

    if (!response.ok) {
        throw new Error(
            `No se pudo cargar ${path}: ${response.status} ${response.statusText}`
        );
    }

    return response.json();
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
        loadJSON("../catalog.json"),
        loadJSON("../authors.json"),
        loadJSON("../categories.json")
    ]);

    VitaHubData.catalog = catalog;
    VitaHubData.authors = authors;
    VitaHubData.categories = categories;

    VitaHubData.loaded = true;

    return VitaHubData;
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