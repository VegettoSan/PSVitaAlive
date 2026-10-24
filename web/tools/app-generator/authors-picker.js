(() => {
    "use strict";

    const RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main";
    const AUTHOR_FALLBACK = `${RAW_BASE}/authors/icon/autoricon.png`;
    const $ = (id) => document.getElementById(id);
    const select = $("authors");
    if (!select) return;

    const createdAuthors = new Map();
    // Imported files already exist on the contributor's PC; never export them
    // again as new author JSON files when downloading an application.
    const importedAuthors = new Map();

    function getOptions() {
        return [...select.options];
    }

    function syncHiddenSelect(ids) {
        const selected = new Set(ids);
        getOptions().forEach(option => {
            option.selected = selected.has(option.value);
        });
        select.dispatchEvent(new Event("change", { bubbles: true }));
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

    function selectedIds() {
        return getOptions().filter(option => option.selected).map(option => option.value);
    }

    function renderSelected() {
        const container = $("selected-authors");
        if (!container) return;
        const ids = selectedIds();
        container.innerHTML = ids.map(id => {
            const option = getOptions().find(item => item.value === id);
            const label = option ? option.textContent : id;
            return `<span class="author-chip"><span>${escapeHtml(label)}</span><button type="button" data-author-remove="${escapeHtml(id)}" aria-label="Remove ${escapeHtml(label)}">×</button></span>`;
        }).join("");
        container.querySelectorAll("[data-author-remove]").forEach(button => {
            button.addEventListener("click", () => {
                const ids = selectedIds().filter(id => id !== button.dataset.authorRemove);
                syncHiddenSelect(ids);
                renderSelected();
                renderResults($("author-search").value);
            });
        });
        updateCreateNote();
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

    function renderResults(query = "") {
        const results = $("author-results");
        if (!results) return;

        const needle = query.trim().toLocaleLowerCase();
        const selected = new Set(selectedIds());

        if (!needle) {
            results.innerHTML = `<div class="author-empty">Start typing an author name or ID to search.</div>`;
            return;
        }

        const matches = getOptions().filter(option => {
            if (selected.has(option.value)) return false;
            return option.textContent.toLocaleLowerCase().includes(needle) || option.value.toLocaleLowerCase().includes(needle);
        }).slice(0, 12);

        if (matches.length) {
            results.innerHTML = matches.map(option => `<button type="button" class="author-result" data-author-id="${escapeHtml(option.value)}"><strong>${escapeHtml(option.textContent.split(" — ")[0])}</strong><small>${escapeHtml(option.value)}</small></button>`).join("");
        } else {
            results.innerHTML = `
                <div class="author-empty">No matching author found.</div>
                <div class="author-create-actions">
                    <button id="create-author" type="button" class="small-button">+ Create new author</button>
                </div>`;
            $("create-author").addEventListener("click", showCreateDialog);
        }

        results.querySelectorAll("[data-author-id]").forEach(button => {
            button.addEventListener("click", () => {
                const ids = selectedIds();
                if (!ids.includes(button.dataset.authorId)) ids.push(button.dataset.authorId);
                syncHiddenSelect(ids);
                renderSelected();
                $("author-search").value = "";
                renderResults("");
            });
        });
    }

    function escapeHtml(value) {
        return String(value).replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;").replaceAll("'", "&#039;");
    }

    function slugify(value) {
        return String(value).normalize("NFKD").replace(/[\u0300-\u036f]/g, "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 80);
    }

    function normalizeAuthorName(value) {
        return String(value || "")
            .normalize("NFKD")
            .replace(/[\u0300-\u036f]/g, "")
            .toLocaleLowerCase()
            .replace(/\s+/g, " ")
            .trim();
    }

    function getExistingAuthorByName(name) {
        const normalized = normalizeAuthorName(name);
        if (!normalized) return null;

        const existing = stateAuthors().find(author => normalizeAuthorName(author.name) === normalized);
        if (existing) return existing;

        for (const author of createdAuthors.values()) {
            if (normalizeAuthorName(author.name) === normalized) return author;
        }
        return null;
    }

    function stateAuthors() {
        return Array.from(select.options).map(option => ({
            id: option.value,
            name: String(option.textContent || "").split(" — ")[0].trim()
        }));
    }

    function getAuthorLinks() {
        return [...document.querySelectorAll(".author-link-item")].map(row => {
            const type = row.querySelector(".author-link-type").value;
            const name = row.querySelector(".author-link-name").value.trim();
            const url = row.querySelector(".author-link-url").value.trim();
            const recommended = row.querySelector(".author-link-recommended").checked;
            return { type, name, url, recommended };
        }).filter(link => link.name || link.url);
    }

    function showCreateDialog() {
        const dialog = $("new-author-dialog");
        if (!dialog) return;
        const searchName = $("author-search").value.trim();
        const existing = getExistingAuthorByName(searchName);
        if (existing) {
            $("new-author-error").textContent = `This author already exists: ${existing.name} (${existing.id}). Search and select that profile instead.`;
            return;
        }
        $("new-author-name").value = searchName;
        $("new-author-id").value = slugify(searchName);
        $("new-author-avatar").value = AUTHOR_FALLBACK;
        $("new-author-icon").value = AUTHOR_FALLBACK;
        $("new-author-bio").value = "";
        $("new-author-links").innerHTML = "";
        addAuthorLinkRow();
        $("new-author-error").textContent = "";
        dialog.hidden = false;
        $("new-author-name").focus();
    }

    function closeCreateDialog() {
        const dialog = $("new-author-dialog");
        if (dialog) dialog.hidden = true;
    }

    function validateAuthorForm() {
        const name = $("new-author-name").value.trim();
        const id = $("new-author-id").value.trim() || slugify(name);
        const avatar = $("new-author-avatar").value.trim();
        const icon = $("new-author-icon").value.trim();
        const error = $("new-author-error");
        error.textContent = "";

        if (!name) {
            error.textContent = "Author name is required.";
            return null;
        }
        if (!/^[a-z0-9_-]+$/.test(id)) {
            error.textContent = "Author ID may only contain lowercase letters, numbers, hyphens and underscores.";
            return null;
        }

        const existingById = getOptions().find(option => option.value === id);
        if (existingById && !createdAuthors.has(id)) {
            const existingName = String(existingById.textContent || "").split(" — ")[0].trim();
            error.textContent = `That author ID already exists (${existingName}). Search for it instead.`;
            return null;
        }

        const existingByName = getExistingAuthorByName(name);
        if (existingByName && !createdAuthors.has(existingByName.id)) {
            error.textContent = `That author already exists as '${existingByName.name}' (ID: ${existingByName.id}). Search for that profile instead.`;
            return null;
        }

        for (const [label, url] of [["Avatar", avatar], ["Icon", icon]]) {
            if (url && !isHttpUrl(url)) {
                error.textContent = `${label} must use an http:// or https:// URL.`;
                return null;
            }
        }
        const links = getAuthorLinks();
        const recommended = links.filter(link => link.recommended).length;
        if (recommended > 1) {
            error.textContent = "At most one author link may be recommended.";
            return null;
        }
        for (const [index, link] of links.entries()) {
            if (!link.name || !link.url) {
                error.textContent = `Author link ${index + 1}: name and URL are required.`;
                return null;
            }
            if (!isHttpUrl(link.url)) {
                error.textContent = `Author link ${index + 1}: URL must use http:// or https://.`;
                return null;
            }
        }

        return {
            id,
            name,
            avatar: avatar || AUTHOR_FALLBACK,
            bio: $("new-author-bio").value.trim(),
            links,
            icon: icon || AUTHOR_FALLBACK
        };
    }

    function addAuthorLinkRow(value = {}) {
        const list = $("new-author-links");
        if (!list) return;
        const row = document.createElement("div");
        row.className = "author-link-item";
        row.innerHTML = `
            <select class="author-link-type">
                <option>Repository</option>
                <option>Official Website</option>
                <option>Community</option>
                <option>Documentation</option>
                <option>Other</option>
            </select>
            <input class="author-link-name" type="text" placeholder="Name" value="${escapeHtml(value.name || "")}">
            <input class="author-link-url" type="url" placeholder="https://example.org/author" value="${escapeHtml(value.url || "")}">
            <label class="author-link-check"><input class="author-link-recommended" type="checkbox" ${value.recommended ? "checked" : ""}> Recommended</label>
            <button type="button" class="remove-button">Remove</button>`;
        if (value.type) row.querySelector(".author-link-type").value = value.type;
        row.querySelectorAll("input,select").forEach(element => element.addEventListener("input", () => { $("new-author-error").textContent = ""; }));
        row.querySelector(".remove-button").addEventListener("click", () => row.remove());
        list.appendChild(row);
    }

    function createLocalAuthor() {
        const author = validateAuthorForm();
        if (!author) return;

        createdAuthors.set(author.id, author);
        let option = getOptions().find(item => item.value === author.id);
        if (!option) {
            option = document.createElement("option");
            option.value = author.id;
            select.appendChild(option);
        }
        option.textContent = `${author.name} — ${author.id}`;
        syncHiddenSelect([...selectedIds(), author.id]);
        renderSelected();
        $("author-search").value = "";
        renderResults("");
        closeCreateDialog();
        updateCreateNote();
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

    function updateCreateNote() {
        const note = $("author-create-note");
        if (!note) return;
        const selectedCreated = selectedIds().filter(id => createdAuthors.has(id));
        const selectedImported = selectedIds().filter(id => importedAuthors.has(id));
        const notes = [];
        if (selectedCreated.length) {
            notes.push(`New author profile(s): ${selectedCreated.join(", ")}. Download includes ${selectedCreated.length} new author JSON(s).`);
        }
        if (selectedImported.length) {
            notes.push(`Imported author(s): ${selectedImported.join(", ")}. The app references their IDs; existing author JSON files are not downloaded again. Submit the original files to authors/ if not published.`);
        }
        note.textContent = notes.join(" ");
    }

    function isHttpUrl(value) {
        try {
            const url = new URL(value);
            return url.protocol === "http:" || url.protocol === "https:";
        } catch {
            return false;
        }
    }

    function parseImportedAuthorProfile(value) {
        if (!value || typeof value !== "object" || Array.isArray(value)) {
            throw new Error("Expected an author JSON object (not an app or an array).");
        }
        if (typeof value.id !== "string" || !/^[a-z0-9_-]+$/.test(value.id)) {
            throw new Error("Author ID must use lowercase letters, numbers, hyphens or underscores.");
        }
        if (typeof value.name !== "string" || !value.name.trim()) {
            throw new Error("Missing author name.");
        }
        if (Object.hasOwn(value, "title_id") || Object.hasOwn(value, "author_ids")) {
            throw new Error("This looks like an application JSON. Import a file from authors/.");
        }
        for (const field of ["avatar", "bio", "icon"]) {
            if (value[field] !== undefined && typeof value[field] !== "string") {
                throw new Error("Author " + field + " must be text.");
            }
        }
        for (const field of ["avatar", "icon"]) {
            if (value[field] && !isHttpUrl(value[field])) {
                throw new Error("Author " + field + " must use http:// or https://.");
            }
        }
        if (value.links !== undefined && !Array.isArray(value.links)) {
            throw new Error("Author links must be an array.");
        }
        let recommended = 0;
        (value.links || []).forEach((link, index) => {
            if (!link || typeof link !== "object" || Array.isArray(link) ||
                typeof link.type !== "string" || !link.type.trim() ||
                typeof link.name !== "string" || !link.name.trim() ||
                typeof link.url !== "string" || !isHttpUrl(link.url)) {
                throw new Error("Author link " + (index + 1) + " needs a type, name and valid URL.");
            }
            if (link.recommended !== undefined && typeof link.recommended !== "boolean") {
                throw new Error("Author link " + (index + 1) + ": recommended must be true or false.");
            }
            if (link.recommended) recommended++;
        });
        if (recommended > 1) throw new Error("Only one author link may be recommended.");
        return value;
    }

    function registerImportedAuthor(author) {
        const existingOption = getOptions().find(option => option.value === author.id);
        const byName = getExistingAuthorByName(author.name);
        if (existingOption) {
            const existingName = String(existingOption.textContent || "").split(" — ")[0].trim();
            if (normalizeAuthorName(existingName) !== normalizeAuthorName(author.name)) {
                throw new Error("Author ID " + author.id + " already belongs to '" + existingName + "'.");
            }
            if (createdAuthors.has(author.id)) {
                throw new Error("Author '" + author.name + "' is already a new draft in this session.");
            }
            if (!importedAuthors.has(author.id)) {
                // Author already published in authors.json: use the official profile.
                syncHiddenSelect([...new Set([...selectedIds(), author.id])]);
                renderSelected();
                return "Already published: selected " + author.name + " (" + author.id + ").";
            }
            // Reimporting the same author is idempotent; don't create another record.
            syncHiddenSelect([...new Set([...selectedIds(), author.id])]);
            renderSelected();
            return "Already imported: selected " + author.name + " (" + author.id + ").";
        }
        if (byName && byName.id !== author.id) {
            throw new Error("Author name '" + author.name + "' already uses ID " + byName.id + ".");
        }

        importedAuthors.set(author.id, author);
        const option = document.createElement("option");
        option.value = author.id;
        option.textContent = author.name.trim() + " — " + author.id;
        select.appendChild(option);
        syncHiddenSelect([...new Set([...selectedIds(), author.id])]);
        renderSelected();
        return "Imported and selected " + author.name + " (" + author.id + ").";
    }

    async function importAuthorFiles(event) {
        const input = event.target;
        const files = [...(input.files || [])];
        if (!files.length) return;
        const feedback = $("author-import-status");
        const notices = [];
        let failures = 0;
        for (const file of files) {
            try {
                if (file.size > 1024 * 1024) throw new Error("Author JSON exceeds 1 MB.");
                const data = JSON.parse(await file.text());
                const author = parseImportedAuthorProfile(data);
                notices.push(registerImportedAuthor(author));
            } catch (error) {
                failures++;
                notices.push(file.name + ": " + error.message);
            }
        }
        if (feedback) {
            feedback.textContent = notices.join(" ");
            feedback.classList.toggle("import-error", failures > 0);
        }
        input.value = "";
        renderResults($("author-search")?.value || "");
        updateCreateNote();
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

    function downloadBlob(filename, object) {
        const blob = new Blob([JSON.stringify(object, null, 2) + "\n"], { type: "application/json;charset=utf-8" });
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = filename;
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        URL.revokeObjectURL(url);
    }

    function downloadCreatedAuthorJson() {
        const author = validateAuthorForm();
        if (!author) return;
        createdAuthors.set(author.id, author);
        downloadBlob(`${author.id}.json`, author);
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

    window.getCreatedAuthorProfiles = () => selectedIds().map(id => createdAuthors.get(id)).filter(Boolean);
    window.isCreatedAuthor = (id) => createdAuthors.has(id) || importedAuthors.has(id);
    window.getSelectedImportedAuthorIds = () => selectedIds().filter(id => importedAuthors.has(id));
    window.restoreImportedAuthorSelection = (ids) => {
        syncHiddenSelect((ids || []).filter(id => importedAuthors.has(id) && getOptions().some(o => o.value === id)));
        renderSelected();
    };
    window.restoreLocalAuthorOptions = () => {
        for (const author of [...createdAuthors.values(), ...importedAuthors.values()]) {
            if (getOptions().some(option => option.value === author.id)) continue;
            const option = document.createElement("option");
            option.value = author.id;
            option.textContent = author.name + " — " + author.id;
            select.appendChild(option);
        }
    };
    window.clearCreatedAuthors = () => {
        createdAuthors.forEach((_, id) => {
            const option = getOptions().find(item => item.value === id);
            if (option) option.remove();
        });
        createdAuthors.clear();
        if (typeof window.refreshAuthorPicker === "function") window.refreshAuthorPicker();
    };
    window.downloadAuthorProfiles = (authors) => {
        authors.forEach(author => downloadBlob(`${author.id}.json`, author));
    };

    function setup() {
        const wrapper = $("author-picker");
        if (!wrapper) return;
        select.classList.add("author-source-select");
        select.setAttribute("aria-hidden", "true");
        select.tabIndex = -1;

        wrapper.innerHTML = `
            <div class="author-picker-box">
                <div id="selected-authors" class="selected-authors"></div>
                <input id="author-search" class="author-search" type="search" autocomplete="off" placeholder="Search authors by name or ID..." aria-label="Search authors">
            </div>
            <div id="author-results" class="author-results" role="listbox"></div>
            <div class="author-picker-actions">
                <label class="author-import-button">Import author JSON
                    <input type="file" id="import-author-json" accept=".json,application/json" multiple aria-label="Import author JSON file">
                </label>
                <span class="author-import-help">Reuse an existing authors/*.json file before it is published.</span>
            </div>
            <p id="author-import-status" class="author-import-status" role="status" aria-live="polite"></p>
            <p id="author-create-note" class="author-create-note"></p>
            <div id="new-author-dialog" class="new-author-dialog" hidden>
                <div class="new-author-dialog-card" role="dialog" aria-modal="true" aria-labelledby="new-author-title">
                    <h3 id="new-author-title">Create author profile</h3>
                    <p>Fill in the author profile. If you create a new author, the final generator download will include the author JSON and the application JSON.</p>
                    <div class="author-dialog-grid">
                        <label>Author name<input id="new-author-name" type="text" required></label>
                        <label>Author ID<input id="new-author-id" type="text" placeholder="generated-from-name" required></label>
                        <label>Avatar URL<input id="new-author-avatar" type="url" placeholder="https://example.org/avatar.png"></label>
                        <label>Icon URL<input id="new-author-icon" type="url" placeholder="https://example.org/icon.png"></label>
                        <label class="author-dialog-full">Bio<textarea id="new-author-bio" rows="4" placeholder="Short author biography..."></textarea></label>
                        <div class="author-dialog-full">
                            <div class="author-links-heading"><strong>Author links</strong><button id="add-author-link" type="button" class="small-button">+ Add link</button></div>
                            <div id="new-author-links" class="author-links-list"></div>
                        </div>
                    </div>
                    <p id="new-author-error" class="author-create-error"></p>
                    <div class="dialog-actions"><button id="cancel-author" type="button" class="small-button">Cancel</button><button id="save-author" type="button" class="small-button primary-small-button">Add author</button><button id="download-author" type="button" class="small-button">Download author JSON</button></div>
                </div>
            </div>`;

        $("author-search").addEventListener("input", event => renderResults(event.target.value));
        $("import-author-json").addEventListener("change", importAuthorFiles);
        $("cancel-author").addEventListener("click", closeCreateDialog);
        $("save-author").addEventListener("click", createLocalAuthor);
        $("download-author").addEventListener("click", downloadCreatedAuthorJson);
        $("add-author-link").addEventListener("click", () => addAuthorLinkRow());
        $("new-author-name").addEventListener("input", event => {
            const currentId = $("new-author-id").value;
            const previousSlug = slugify(event.target.dataset.previousName || "");
            if (!currentId || currentId === previousSlug) $("new-author-id").value = slugify(event.target.value);
            event.target.dataset.previousName = event.target.value;
            $("new-author-error").textContent = "";
        });
        $("new-author-id").addEventListener("input", () => { $("new-author-error").textContent = ""; });

        const observer = new MutationObserver(() => {
            renderSelected();
            renderResults($("author-search")?.value || "");
        });
        observer.observe(select, { childList: true });

        renderSelected();
        renderResults("");
    }

    window.refreshAuthorPicker = () => {
        if ($("author-picker")) {
            renderSelected();
            renderResults($("author-search")?.value || "");
        }
    };

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", setup, { once: true });
    } else {
        setup();
    }
})();
