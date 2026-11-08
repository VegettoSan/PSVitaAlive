#!/usr/bin/env node
/**
 * Exercise actual web data loaders (not only the generic key codec) with ALL
 * six public catalog files in V1 and V2 forms; no network or server required.
 *
 * Input locations are generated inside CI, never published to main:
 *   /tmp/catalog-v1/*.json                Homebrew V1
 *   ./catalog.json, authors.json,...      Homebrew V2
 *   ./catalog_*_games.json                Games V1
 *   /tmp/psva-game-catalogs-v2/*.json     Games V2
 *
 *   node scripts/catalog_schema_v2_readers_test.cjs
 */
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const ROOT = path.resolve(__dirname, '..');
const V1_HOME = '/tmp/catalog-v1';
const V2_HOME = ROOT;
const V1_GAMES = ROOT;
const V2_GAMES = '/tmp/psva-game-catalogs-v2';
const HOME = ['catalog.json', 'authors.json', 'categories.json'];
const GAMES = ['catalog_psvita_games.json', 'catalog_psp_games.json', 'catalog_ps1_games.json'];
const ALL = [...HOME, ...GAMES];
const PAGE_FILES = ['index.html', 'app.html', 'author.html', 'authors.html',
                    'category.html', 'game.html'];

function readFiles(version) {
    const files = new Map();
    for (const name of ALL) {
        const folder = HOME.includes(name)
            ? (version === 'v1' ? V1_HOME : V2_HOME)
            : (version === 'v1' ? V1_GAMES : V2_GAMES);
        files.set(name, fs.readFileSync(path.join(folder, name)));
    }
    return files;
}

function makeFetch(files, stream) {
    return async url => {
        const name = String(url).split('/').pop().split('?')[0];
        assert(files.has(name), 'unexpected URL ' + url);
        const bytes = files.get(name);
        let pos = 0;
        return {
            ok: true,
            status: 200,
            statusText: 'OK',
            headers: { get: field => field.toLowerCase() === 'content-length' ? String(bytes.length) : null },
            json: async () => JSON.parse(bytes.toString('utf8')),
            body: stream
                ? { getReader: () => ({ read: async () => {
                    if (pos >= bytes.length) return { done: true, value: undefined };
                    const end = Math.min(bytes.length, pos + 32768);
                    const val = new Uint8Array(bytes.subarray(pos, end));
                    pos = end;
                    return { done: false, value: val };
                } }) }
                : null,
        };
    };
}

function makeBrowser(files, stream = false) {
    const context = {
        console,
        fetch: makeFetch(files, stream),
        URLSearchParams,
        TextDecoder,
        Uint8Array,
        Date,
        setTimeout,
        clearTimeout,
        document: {
            title: '',
            addEventListener: () => {},
            getElementById: () => null,
            querySelector: () => null,
        },
        location: { search: '', href: '' },
    };
    context.window = context;
    vm.createContext(context);
    for (const file of ['web/js/catalog-schema-v2.js', 'web/js/data.js']) {
        vm.runInContext(fs.readFileSync(path.join(ROOT, file), 'utf8'), context, { filename: file });
    }
    return context;
}

function jsonEq(actual, expected, name) {
    assert.equal(JSON.stringify(actual), JSON.stringify(expected), name);
}

async function testWebData(version, originals) {
    const files = readFiles(version);
    const browser = makeBrowser(files);
    const loaded = await vm.runInContext('loadVitaHubData()', browser);
    for (const [file, key] of [['catalog.json', 'catalog'], ['authors.json', 'authors'],
                               ['categories.json', 'categories']]) {
        jsonEq(loaded[key], originals[file], version + ' web loader: ' + file);
    }
    const vita = await vm.runInContext('loadOfficialGames()', browser);
    jsonEq(vita, originals[GAMES[0]], version + ' Vita games via data.js');
    vm.runInContext(fs.readFileSync(path.join(ROOT, 'web/js/multi-game-page.js'), 'utf8'),
                    browser, {filename: 'web/js/multi-game-page.js'});
    for (const [catalog, file] of [['psvita', GAMES[0]], ['psp', GAMES[1]], ['ps1', GAMES[2]]]) {
        browser.location.search = '?catalog=' + catalog;
        const games = await browser.loadOfficialGamesCatalog();
        jsonEq(games, originals[file], version + ' game detail page: ' + file);
    }
    console.log(version + ': data.js and all game detail page loaders OK');
}

async function testSwitcher(version, originals, streamed) {
    const browser = makeBrowser(readFiles(version), streamed);
    vm.runInContext(fs.readFileSync(path.join(ROOT, 'web/js/catalog-switcher.js'), 'utf8'),
                    browser, {filename:'web/js/catalog-switcher.js'});
    for (const [catalog, file] of [['psvita', GAMES[0]], ['psp', GAMES[1]], ['ps1', GAMES[2]]]) {
        const parsed = await vm.runInContext(
            'fetchGameCatalogWithProgress(' + JSON.stringify(catalog) + ')', browser);
        jsonEq(parsed, originals[file],
               version + ' ' + (streamed ? 'streaming' : 'buffered') + ' catalog switcher: ' + file);
        const cacheHit = await vm.runInContext(
            'fetchGameCatalogWithProgress(' + JSON.stringify(catalog) + ')', browser);
        assert.strictEqual(parsed, cacheHit, 'cached game catalog must return original array');
    }
    console.log(version + ': game switcher ' + (streamed ? 'streaming' : 'buffered') + ' and cache OK');
}

function checkScriptOrder() {
    for (const p of PAGE_FILES) {
        const html = fs.readFileSync(path.join(ROOT, 'web', p), 'utf8');
        const schema = html.indexOf('src="js/catalog-schema-v2.js"');
        const data = html.indexOf('src="js/data.js"');
        assert(schema !== -1 && data !== -1 && schema < data,
               p + ': adapter must load before data.js');
        for (const script of [...html.matchAll(/<script[^>]+src="([^"]+)"/g)]) {
            const local = script[1].split('?')[0];
            if (!local.startsWith('http')) {
                assert(fs.existsSync(path.join(ROOT, 'web', local)), p + ': missing JS ' + local);
            }
        }
    }
    const generator = fs.readFileSync(path.join(ROOT,'web/tools/app-generator/index.html'),'utf8');
    assert(generator.indexOf('src="../../js/catalog-schema-v2.js"') <
           generator.indexOf('src="app-generator.js"'),
           'app generator must load catalog adapter before form scripts');
    console.log('All public pages: V2 script dependencies/load order OK');
}

async function main() {
    const originals = Object.fromEntries(
        [...readFiles('v1').entries()].map(([name, buf]) => [name, JSON.parse(buf.toString('utf8'))])
    );
    checkScriptOrder();
    for (const version of ['v1', 'v2']) {
        await testWebData(version, originals);
        await testSwitcher(version, originals, false);
        await testSwitcher(version, originals, true);
    }
    console.log('PASS: all six catalogs decoded by real web loaders in V1 and V2, including streaming.');
}

main().catch(error => { console.error(error.stack || error); process.exit(1); });
