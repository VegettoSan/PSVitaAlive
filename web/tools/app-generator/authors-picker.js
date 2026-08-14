(() => {
    "use strict";

    const $ = (id) => document.getElementById(id);
    const select = $("authors");
    if (!select) return;

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

    function showCreateDialog() {
        const dialog = $("new-author-dialog");
        if (!dialog) return;
        $("new-author-name").value = $("author-search").value.trim();
        dialog.hidden = false;
        $("new-author-name").focus();
    }

    function closeCreateDialog() {
        const dialog = $("new-author-dialog");
        if (dialog) dialog.hidden = true;
    }

    function slugify(value) {
        return String(value).normalize("NFKD").replace(/[\u0300-\u036f]/g, "").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 80);
    }

    function createLocalAuthor() {
        const name = $("new-author-name").value.trim();
        const id = $("new-author-id").value.trim() || slugify(name);
        const error = $("new-author-error");
        error.textContent = "";
        if (!name) {
            error.textContent = "Author name is required.";
            return;
        }
        if (!/^[a-z0-9_-]+$/.test(id)) {
            error.textContent = "Author ID may only contain lowercase letters, numbers, hyphens and underscores.";
            return;
        }
        if (getOptions().some(option => option.value === id)) {
            error.textContent = "That author ID already exists. Search for it instead.";
            return;
        }

        const option = document.createElement("option");
        option.value = id;
        option.textContent = `${name} — ${id}`;
        select.appendChild(option);
        syncHiddenSelect([...selectedIds(), id]);
        renderSelected();
        renderResults("");
        closeCreateDialog();

        const note = $("author-create-note");
        if (note) note.textContent = `Created ${id} for this JSON. Remember: this browser tool cannot commit authors/${id}.json to GitHub automatically; the profile must be added to the repository before the app can pass repository validation.`;
        if (typeof window.updatePreview === "function") window.updatePreview();
    }

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
                    <h3 id="new-author-title">Create author profile reference</h3>
                    <p>This adds the new author to the generated app JSON. It does not commit a file to GitHub.</p>
                    <label>Author name<input id="new-author-name" type="text"></label>
                    <label>Author ID<input id="new-author-id" type="text" placeholder="generated-from-name"></label>
                    <p id="new-author-error" class="author-create-error"></p>
                    <div class="dialog-actions"><button id="cancel-author" type="button" class="small-button">Cancel</button><button id="save-author" type="button" class="small-button primary-small-button">Add author</button></div>
                </div>
            </div>`;

        $("author-search").addEventListener("input", event => renderResults(event.target.value));
        $("create-author").addEventListener("click", showCreateDialog);
        $("cancel-author").addEventListener("click", closeCreateDialog);
        $("save-author").addEventListener("click", createLocalAuthor);
        $("new-author-id").addEventListener("input", () => { $("new-author-error").textContent = ""; });

        // app-generator.js fills the select asynchronously after fetching authors.json.
        // Observe those option changes so the custom picker always reflects the official catalog.
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
