#!/usr/bin/env node
'use strict';

// Tests the real browser scripts without a network or a browser installation.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const ROOT = path.resolve(__dirname, '../..');
const DATA = {
    'catalog.json': [{ id: 'homebrew' }],
    'authors.json': [{ id: 'author' }],
    'categories.json': [{ id: 'category' }],
    'catalog_psvita_games.json': [{ id: 'vita-game' }],
    'catalog_psp_games.json': [{ id: 'psp-game' }],
};

function browser(withHomePageLoader, failCatalog = false) {
    const requests = [];
    const context = {
        console,
        Date: { now: () => 1234567890 }, // Fixed time proves requests are unique.
        fetch: async (url, options) => {
            requests.push({ url: String(url), options });
            const name = String(url).split('?')[0].split('/').pop();
            assert.ok(Object.hasOwn(DATA, name), 'unexpected catalog ' + url);
            if (failCatalog && name === 'catalog.json') {
                return { ok: false, status: 503, statusText: 'Unavailable' };
            }
            return { ok: true, json: async () => DATA[name] };
        },
    };
    context.window = context;
    context.PSVitaAliveCatalogSchemaV2 = { toV1: x => x };
    vm.createContext(context);
    for (const file of ['web/js/data.js', ...(withHomePageLoader ? ['web/js/catalog-loader.js'] : [])]) {
        vm.runInContext(fs.readFileSync(path.join(ROOT, file), 'utf8'), context, {
            filename: file,
        });
    }
    return { context, requests };
}

async function verify(withHomePageLoader) {
    const { context, requests } = browser(withHomePageLoader);
    const data = await vm.runInContext('loadVitaHubData()', context);
    assert.equal(data.catalog[0].id, 'homebrew');
    assert.equal(data.authors[0].id, 'author');
    assert.equal(data.categories[0].id, 'category');
    assert.equal(data.loaded, true);

    // Do not download the multi-megabyte catalog repeatedly within a page.
    await vm.runInContext('loadVitaHubData()', context);
    assert.equal(requests.length, 3);

    await vm.runInContext('loadOfficialGames()', context);
    await vm.runInContext('loadJSON(VITAHUB_RAW_BASE + "/catalog_psp_games.json")', context);
    assert.equal(requests.length, 5);
    assert.equal(new Set(requests.map(call => call.url)).size, requests.length);
    for (const { url, options } of requests) {
        assert.match(url, /^https:\/\/raw\.githubusercontent\.com\/VegettoSan\/PSVitaAlive\/main\//);
        assert.match(url, /[?&]psva_refresh=1234567890-\d+$/);
        assert.equal(options.cache, 'no-store');
    }

    assert.equal(
        vm.runInContext('freshVitaHubCatalogUrl("https://example.org/file.json")', context),
        'https://example.org/file.json',
        'URLs outside canonical generated catalogs must remain unchanged'
    );
    assert.match(
        vm.runInContext('freshVitaHubCatalogUrl(VITAHUB_RAW_BASE + "/authors.json?foo=1")', context),
        /\?foo=1&psva_refresh=1234567890-\d+$/,
        'existing query params must be preserved'
    );
}

async function verifyFailure() {
    const { context } = browser(true, true);
    await assert.rejects(
        vm.runInContext('loadVitaHubData()', context),
        /503/
    );
    assert.equal(vm.runInContext('VitaHubData.loaded', context), false);
    assert.equal(vm.runInContext('VitaHubData.catalog.length', context), 0);
}

(async () => {
    await verify(false); // app/author/category pages
    await verify(true);  // homepage overrides loadJSON with progress loader
    await verifyFailure();
    console.log('PASS: all public JSON fetches bypass stale caches, both loaders, safe failure.');
})().catch(error => {
    console.error(error.stack || error);
    process.exitCode = 1;
});
