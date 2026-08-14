(() => {
    "use strict";

    const RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main";
    const AUTHOR_FALLBACK = `${RAW_BASE}/authors/icon/autoricon.png`;
    const $ = (id) => document.getElementById(id);
    const select = $("authors");
    if (!select) return;

    const createdAuthors = new Map();

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
        const matches = getOptions().filter(option => {
            if (selected.has(option.value)) return false;
            return !needle || option.textContent.toLocaleLowerCase().includes(needle) || option.value.toLocaleLowerCase().includes(needle);
        }).slice(0, 12);

        results.innerHTML = matches.map(option => `<button type="button" class="author-result" data-author-id="${escapeHtml(option.value)}"><strong>${escapeHtml(option.textContent.split(" — ")[0])}</strong><small>${escapeHtml(option.value)}</small></button>`).join("");
        if (!matches.length) {
            results.innerHTML = `<div class="author-empty">No matching author found.</div>`;
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
        $("new-author-name").value = $("author-search").value.trim();
        $("new-author-id").value = slugify($("new-author-name").value);
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
        if (getOptions().some(option => option.value === id) && !createdAuthors.has(id)) {
            error.textContent = "That author ID already exists. Search for it instead.";
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
        renderResults("");
        closeCreateDialog();
        updateCreateNote();
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

    function updateCreateNote() {
        const note = $("author-create-note");
        if (!note) return;
        const selectedCreated = selectedIds().filter(id => createdAuthors.has(id));
        if (!selectedCreated.length) {
            note.textContent = "";
            return;
        }
        note.textContent = `New author profile${selectedCreated.length > 1 ? "s" : ""}: ${selectedCreated.join(", ")}. The final download will include ${selectedCreated.length} author JSON file${selectedCreated.length > 1 ? "s" : ""} plus the application JSON.`;
    }

    function isHttpUrl(value) {
        try {
            const url = new URL(value);
            return url.protocol === "http:" || url.protocol === "https:";
        } catch {
            return false;
        }
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
    }

    window.getCreatedAuthorProfiles = () => selectedIds().map(id => createdAuthors.get(id)).filter(Boolean);
    window.isCreatedAuthor = (id) => createdAuthors.has(id);
    window.clearCreatedAuthors = () => createdAuthors.clear();
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
                <button id="create-author" type="button" class="small-button">+ Create new author</button>
            </div>
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
        $("create-author").addEventListener("click", showCreateDialog);
        $("cancel-author").addEventListener("click", closeCreateDialog);
        $("save-author").addEventListener("click", createLocalAuthor);
        $("download-author").addEventListener("click", downloadCreatedAuthorJson);
        $("add-author-link").addEventListener("click", () => addAuthorLinkRow());
        $("new-author-name").addEventListener("input", event => {
            if (!$("new-author-id").value || $("new-author-id").value === slugify(event.target.value)) {
                $("new-author-id").value = slugify(event.target.value);
            }
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
