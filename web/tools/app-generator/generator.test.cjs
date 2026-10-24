"use strict";

const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const dir = __dirname;
const source = fs.readFileSync(path.join(dir, "app-generator.js"), "utf8");
const page = fs.readFileSync(path.join(dir, "index.html"), "utf8");

// Compile the entire production script. The actual browser DOM is not needed
// for this syntax check.
test("generator JavaScript is syntactically valid", () => {
    assert.doesNotThrow(() => new vm.Script(source, { filename: "app-generator.js" }));
});

function section(start, end) {
    const from = source.indexOf(start);
    const to = source.indexOf(end, from + start.length);
    assert.ok(from !== -1 && to !== -1, "Could not isolate " + start);
    return source.slice(from, to);
}

// Test production logic, not a duplicated whitelist or validation function.
// The real IIFE's startup loads remote catalogs, so isolate its pure parts.
const executable = [
    section("    const EXTRACT_PATH_TYPES = new Set([", "    const PLUGIN_SECTIONS = ["),
    section("    function linkNeedsExtractPath(type) {", "    /** Build Internal ID"),
    section("    function syncLinkExtractPathVisibility(row) {", "    async function loadJson(path) {"),
    section("    function collectLinks() {", "    function collectApp() {"),
    section("    function validateApp(app) {", "    function issueElement(target) {")
].join("\n");

let currentRows = [];
let screenshotRows = [];
const state = {
    ready: true, mode: "create", catalog: [],
    authors: [{ id: "dev" }],
    categories: [{ id: "games", subcategories: [{ id: "action" }] }]
};
const context = {
    state, URL,
    window: { isCreatedAuthor: () => false },
    parseSizeToBytes: value => value ? Math.round(Number(value) * 1048576) : null,
    validUrl: value => {
        try { return ["http:", "https:"].includes(new URL(value).protocol); }
        catch { return false; }
    },
    document: {
        querySelectorAll: selector => selector === ".link-item" ? currentRows :
            selector === ".screenshot-url" ? screenshotRows : []
    }
};
const api = vm.runInNewContext(executable +
    "\n({ linkNeedsExtractPath, syncLinkExtractPathVisibility, collectLinks, validateApp })", context);

function visibilityRow(type) {
    const typeEl = { value: type };
    const input = { value: "ux0:data/", title: "" };
    const wrap = { hidden: false, style: {}, querySelector: () => input };
    const pluginFields = { style: {} };
    const secSel = { value: "*KERNEL" };
    const custom = { style: {} };
    const elements = {
        ".link-type": typeEl, ".link-extract-wrap": wrap,
        ".link-plugin-fields": pluginFields, ".link-plugin-section": secSel,
        ".link-plugin-section-custom": custom
    };
    return {
        dataset: {}, typeEl, input, wrap, pluginFields,
        querySelector: selector => elements[selector] || null,
        querySelectorAll: () => []
    };
}

test("only ZIP types and Plugin expose a destination", () => {
    for (const type of ["Data Files", "Game Files", "Mod", "Mod Pack", "Patch"]) {
        assert.equal(api.linkNeedsExtractPath(type), true, type);
        const row = visibilityRow(type);
        api.syncLinkExtractPathVisibility(row);
        assert.equal(row.wrap.hidden, false, type);
    }
    for (const type of ["Download", "PKG", "DLC", "Update", "Mirror",
        "Repository", "Official Website", "Documentation", "Issues", "Community", "Other"]) {
        assert.equal(api.linkNeedsExtractPath(type), false, type);
        const row = visibilityRow(type);
        api.syncLinkExtractPathVisibility(row);
        assert.equal(row.wrap.hidden, true, type);
        assert.equal(row.pluginFields.style.display, "none", type);
    }
    const plugin = visibilityRow("Plugin");
    api.syncLinkExtractPathVisibility(plugin);
    assert.equal(plugin.wrap.hidden, false);
    assert.equal(plugin.pluginFields.style.display, "");
});

test("ZIP and Plugin destinations survive type changes independently", () => {
    const row = visibilityRow("Download");
    api.syncLinkExtractPathVisibility(row);
    row.typeEl.value = "Data Files";
    api.syncLinkExtractPathVisibility(row);
    row.input.value = "ux0:app/GAME12345/";
    row.typeEl.value = "Plugin";
    api.syncLinkExtractPathVisibility(row);
    assert.equal(row.input.value, "ur0:tai/");
    row.input.value = "ur0:tai/custom/";
    row.typeEl.value = "Game Files";
    api.syncLinkExtractPathVisibility(row);
    assert.equal(row.input.value, "ux0:app/GAME12345/");
    row.typeEl.value = "Plugin";
    api.syncLinkExtractPathVisibility(row);
    assert.equal(row.input.value, "ur0:tai/custom/");
    row.typeEl.value = "Repository";
    api.syncLinkExtractPathVisibility(row);
    assert.equal(row.wrap.hidden, true);
});

function outputRow(type, url = "https://example.org/file.zip", extractPath = "ux0:data/") {
    const elements = {
        ".link-type": { value: type }, ".link-name": { value: "Link" },
        ".link-url": { value: url }, ".link-size-value": { value: "" },
        ".link-size-unit": { value: "MB" },
        ".link-extract-path": { value: extractPath },
        ".link-plugin-section": { value: "*KERNEL" },
        ".link-plugin-section-custom": { value: "" },
        ".link-plugin-line": { value: "ur0:tai/sample.skprx" },
        ".link-plugin-required": { value: "true" },
        ".link-recommended": { checked: true }
    };
    return { querySelector: selector => elements[selector] || null };
}

test("unrelated links omit extract_path while Plugin retains its schema", () => {
    currentRows = [
        outputRow("Download"), outputRow("Mirror"), outputRow("Repository"),
        outputRow("Game Files", "https://example.org/data.zip", "ux0:app/GAME12345/"),
        outputRow("Plugin", "https://example.org/plugin.skprx", "ur0:tai/")
    ];
    const links = api.collectLinks();
    for (const index of [0, 1, 2]) {
        assert.equal(Object.hasOwn(links[index], "extract_path"), false);
    }
    assert.equal(links[3].extract_path, "ux0:app/GAME12345/");
    assert.equal(links[4].extract_path, "ur0:tai/");
    assert.equal(links[4].section, "*KERNEL");
    assert.equal(links[4].line, "ur0:tai/sample.skprx");
    assert.equal(links[4].required, true);
});

test("validation identifies the missing main size and correct link field", () => {
    currentRows = [outputRow("Download")];
    const app = {
        id: "example", title_id: "EXAM00001", name: "Example",
        description: "Short", long_description: "Full",
        author_ids: ["dev"], category_id: "games", subcategory_ids: ["action"],
        version: "1.0", version_date: "2026-10-24", requirements: "None",
        size: 1048576, status: "Verified",
        links: [{ type: "Download", name: "Link", url: "https://example.org/file.vpk" }]
    };
    assert.equal(api.validateApp(app).length, 0);
    assert.ok(api.validateApp({ ...app, size: 0 }).some(error => error.target === "size"));
    assert.ok(api.validateApp({ ...app, author_ids: [] }).some(error => error.target === "authors"));
    assert.ok(api.validateApp({ ...app, links: [{ ...app.links[0], url: "bad url" }] })
        .some(error => error.target === "link:0:url"));
    assert.ok(api.validateApp({ ...app, source_url: "not-a-url" })
        .some(error => error.target === "source_url"));
});

test("the page exposes jump targets, a bottom reset, and an export status", () => {
    for (const id of ["section-identity", "section-authors", "section-release",
        "section-media", "section-links", "validation-summary",
        "create-another", "download-status"]) {
        assert.ok(page.includes('id="' + id + '"'), id);
    }
    assert.ok(source.includes('navigateToIssue(button.dataset.target)'));
    assert.ok(source.includes('$("create-another").addEventListener("click", startNewApp)'));
});
