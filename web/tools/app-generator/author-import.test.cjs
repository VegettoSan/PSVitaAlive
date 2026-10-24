"use strict";

const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "authors-picker.js"), "utf8");

function sliceFunction(start, end) {
    const from = source.indexOf(start);
    const to = source.indexOf(end, from + start.length);
    assert.ok(from >= 0 && to >= 0, "Cannot find " + start);
    return source.slice(from, to);
}

const rawFunctions = [
    sliceFunction("    function parseImportedAuthorProfile(value) {", "    function registerImportedAuthor(author) {"),
    sliceFunction("    function registerImportedAuthor(author) {", "    async function importAuthorFiles(event) {")
].join("\n");

function harness() {
    const createdAuthors = new Map();
    const importedAuthors = new Map();
    const options = [];
    const select = { options, appendChild: option => options.push(option) };
    let renders = 0;
    const normalized = value => String(value || "").toLowerCase().trim();
    const getOptions = () => options;
    const selectedIds = () => options.filter(o => o.selected).map(o => o.value);
    const getExistingAuthorByName = name => {
        const match = options.find(o => normalized(o.textContent.split(" — ")[0]) === normalized(name));
        return match ? { id: match.value, name: match.textContent.split(" — ")[0] } : null;
    };
    const syncHiddenSelect = ids => options.forEach(o => { o.selected = ids.includes(o.value); });
    const renderSelected = () => { renders++; };
    const isHttpUrl = value => {
        try { return ["http:", "https:"].includes(new URL(value).protocol); }
        catch { return false; }
    };
    const context = {
        createdAuthors, importedAuthors, select, getOptions, selectedIds, getExistingAuthorByName,
        normalizeAuthorName: normalized, syncHiddenSelect, renderSelected, isHttpUrl,
        document: { createElement: tag => {
            assert.equal(tag, "option");
            return { value: "", textContent: "", selected: false };
        } }
    };
    const api = vm.runInNewContext(rawFunctions +
        "\n({ parseImportedAuthorProfile, registerImportedAuthor })", context);
    return { api, options, createdAuthors, importedAuthors, selectedIds, getRenderCount: () => renders };
}

const example = () => ({
    id: "developer", name: "Developer", avatar: "", bio: "Homebrew developer",
    links: [{ type: "Repository", name: "Source", url: "https://github.com/example" }]
});

test("author import JavaScript parses without syntax errors", () => {
    assert.doesNotThrow(() => new vm.Script(source));
});

test("accept a valid unpublished author profile and select its ID", () => {
    const h = harness();
    const author = h.api.parseImportedAuthorProfile(example());
    const result = h.api.registerImportedAuthor(author);
    assert.match(result, /Imported and selected/);
    assert.deepEqual(h.selectedIds(), ["developer"]);
    assert.equal(h.importedAuthors.get("developer").name, "Developer");
    assert.ok(h.getRenderCount() > 0);
});

test("reject application JSON and malformed author profiles before changing selection", () => {
    const h = harness();
    const invalid = [
        [],
        { id: "UPPER", name: "Developer", links: [] },
        { id: "developer", name: "", links: [] },
        { ...example(), title_id: "EXAM00001" },
        { ...example(), links: [{ type: "Community", name: "Group", url: "javascript:alert(1)" }] },
        { ...example(), links: [{ type: "Community", name: "Group", url: "https://example.org", recommended: "true" }] },
        { ...example(), links: { type: "Repository" } }
    ];
    invalid.forEach(entry => assert.throws(() => h.api.parseImportedAuthorProfile(entry)));
    assert.equal(h.options.length, 0);
});

test("repeated import reselects same author, without creating a duplicate profile", () => {
    const h = harness();
    const author = h.api.parseImportedAuthorProfile(example());
    h.api.registerImportedAuthor(author);
    h.options[0].selected = false;
    const result = h.api.registerImportedAuthor(author);
    assert.match(result, /Already imported/);
    assert.equal(h.options.length, 1);
    assert.equal(h.importedAuthors.size, 1);
    assert.deepEqual(h.selectedIds(), ["developer"]);
});

test("reject conflicts with a published ID or an existing name", () => {
    const h = harness();
    h.options.push({ value: "existing", textContent: "Maintainer — existing", selected: false });
    assert.throws(() => h.api.registerImportedAuthor({
        ...example(), id: "existing", name: "Impostor"
    }), /already belongs/);
    assert.throws(() => h.api.registerImportedAuthor({
        ...example(), id: "another-id", name: "Maintainer"
    }), /already uses ID/);
    assert.equal(h.importedAuthors.size, 0);
});

test("published author with matching ID is selected but is not imported again", () => {
    const h = harness();
    h.options.push({ value: "developer", textContent: "Developer — developer", selected: false });
    const result = h.api.registerImportedAuthor(example());
    assert.match(result, /Already published/);
    assert.deepEqual(h.selectedIds(), ["developer"]);
    assert.equal(h.importedAuthors.size, 0);
});

test("app export downloads only newly created authors, not imported author profiles", () => {
    assert.match(source, /window\.getCreatedAuthorProfiles = \(\) => selectedIds\(\)\.map\(id => createdAuthors\.get\(id\)\)/);
    assert.match(source, /window\.getSelectedImportedAuthorIds =/);
    assert.match(source, /window\.restoreImportedAuthorSelection =/);
    assert.match(source, /window\.restoreLocalAuthorOptions =/);
    assert.match(source, /id="import-author-json"/);
});
