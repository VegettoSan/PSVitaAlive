(() => {
    "use strict";

    const RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main";
    const DOWNLOAD_TYPES = [
        "Download",
        "PKG",
        "DLC",
        "Update",
        "Patch",
        "Mod",
        "Mod Pack",
        "Data Files",
        "Game Files",
        "Mirror",
        "Repository",
        "Official Website",
        "Documentation",
        "Issues",
        "Community",
        "Plugin",
        "Other"
    ];

    const state = {
        catalog: [],
        authors: [],
        categories: [],
        ready: false,
        mode: "create"
    };

    const $ = (id) => document.getElementById(id);

    function today() {
        return new Date().toISOString().slice(0, 10);
    }

    function escapeHtml(value) {
        return String(value)
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll('"', "&quot;")
            .replaceAll("'", "&#039;");
    }

    const EXTRACT_PATH_TYPES = new Set([
        "Data Files",
        "Game Files",
        "Mod",
        "Mod Pack",
        "Patch"
    ]);

    const PLUGIN_SECTIONS = [
        { id: "*main", label: "*main" },
        { id: "*KERNEL", label: "*KERNEL" },
        { id: "*ALL", label: "*ALL" },
        { id: "custom", label: "Custom…" },
        { id: "none", label: "none (file only)" }
    ];

    const SIZE_UNITS = [
        { id: "B", label: "B", mul: 1 },
        { id: "KB", label: "KB", mul: 1024 },
        { id: "MB", label: "MB", mul: 1024 * 1024 },
        { id: "GB", label: "GB", mul: 1024 * 1024 * 1024 }
    ];

    function sizeUnitOptionsHtml(selected) {
        const sel = selected || "MB";
        return SIZE_UNITS.map(u =>
            `<option value="${u.id}" ${u.id === sel ? "selected" : ""}>${u.label}</option>`
        ).join("");
    }

    function parseSizeToBytes(value, unit) {
        if (value === undefined || value === null || String(value).trim() === "") return null;
        const n = Number(String(value).trim().replace(",", "."));
        if (!Number.isFinite(n) || n < 0) return null;
        const found = SIZE_UNITS.find(u => u.id === unit) || SIZE_UNITS[0];
        return Math.round(n * found.mul);
    }

    /** Convert integer bytes to a friendly value + unit for the form. */
    function bytesToValueUnit(bytes) {
        const n = Number(bytes);
        if (!Number.isFinite(n) || n < 0) return { value: "", unit: "MB" };
        if (n === 0) return { value: "0", unit: "B" };
        if (n >= 1024 * 1024 * 1024) {
            const v = n / (1024 * 1024 * 1024);
            return { value: String(Math.round(v * 1000) / 1000), unit: "GB" };
        }
        if (n >= 1024 * 1024) {
            const v = n / (1024 * 1024);
            return { value: String(Math.round(v * 1000) / 1000), unit: "MB" };
        }
        if (n >= 1024) {
            const v = n / 1024;
            return { value: String(Math.round(v * 1000) / 1000), unit: "KB" };
        }
        return { value: String(n), unit: "B" };
    }

    /** Accept legacy human strings like "1.5 GB" when importing JSON. */
    function parseLegacySizeField(raw) {
        if (raw === undefined || raw === null || raw === "") return { value: "", unit: "MB" };
        if (typeof raw === "number" && Number.isFinite(raw)) return bytesToValueUnit(raw);
        const s = String(raw).trim();
        if (/^\d+$/.test(s)) return bytesToValueUnit(Number(s));
        const m = s.match(/^([\d.,]+)\s*(b|bytes?|kb|kib|mb|mib|gb|gib)?$/i);
        if (!m) return { value: s, unit: "MB" };
        const num = m[1].replace(",", ".");
        let unit = (m[2] || "B").toUpperCase();
        if (unit === "BYTES" || unit === "BYTE") unit = "B";
        if (unit === "KIB") unit = "KB";
        if (unit === "MIB") unit = "MB";
        if (unit === "GIB") unit = "GB";
        if (!["B", "KB", "MB", "GB"].includes(unit)) unit = "MB";
        return { value: num, unit };
    }

    function linkNeedsExtractPath(type) {
        return EXTRACT_PATH_TYPES.has(String(type || ""));
    }

    /** Build Internal ID from display name (create mode). */
    function slugifyId(name) {
        return String(name || "")
            .normalize("NFKD")
            .replace(/[\u0300-\u036f]/g, "")
            .toLowerCase()
            .replace(/[^a-z0-9]+/g, "-")
            .replace(/^-+|-+$/g, "")
            .replace(/-{2,}/g, "-");
    }

    function syncIdFromName() {
        if (state.mode !== "create") return;
        const idEl = $("id");
        const nameEl = $("name");
        if (!idEl || !nameEl) return;
        idEl.value = slugifyId(nameEl.value);
    }

    function applyIdFieldMode() {
        const idEl = $("id");
        const hint = $("id-hint");
        if (!idEl) return;
        if (state.mode === "create") {
            idEl.readOnly = true;
            idEl.classList.add("id-locked");
            idEl.title = "Auto-generated from Name in Create mode";
            if (hint) {
                hint.innerHTML = "Auto-filled from the name in Create mode (e.g. <code>my-cool-app</code> → file <code>my-cool-app.json</code>). Only lowercase, numbers, hyphens and underscores. Not editable in Create mode.";
            }
            syncIdFromName();
        } else {
            idEl.readOnly = false;
            idEl.classList.remove("id-locked");
            idEl.title = "";
            if (hint) {
                hint.innerHTML = "Edit mode: you may keep the existing ID. Changing it renames the JSON file — only do this if you know what you are doing.";
            }
        }
    }


    function syncLinkExtractPathVisibility(row) {
        if (!row) return;
        const typeEl = row.querySelector(".link-type");
        const wrap = row.querySelector(".link-extract-wrap");
        if (!typeEl || !wrap) return;

        const type = typeEl.value;
        const isPlugin = type === "Plugin";
        const show = linkNeedsExtractPath(type) || isPlugin;
        const input = wrap.querySelector(".link-extract-path");
        const previousType = row.dataset.lastLinkType;

        // Preserve ZIP and plugin destinations separately when users change link types.
        // A hidden path must never leak into an unrelated link's generated JSON.
        if (input && previousType && previousType !== type) {
            if (previousType === "Plugin") {
                row.dataset.pluginExtractPath = input.value;
            } else if (linkNeedsExtractPath(previousType)) {
                row.dataset.zipExtractPath = input.value;
            }
            if (show) {
                input.value = isPlugin
                    ? (row.dataset.pluginExtractPath || "ur0:tai/")
                    : (row.dataset.zipExtractPath || "ux0:data/");
            }
        }
        if (input) {
            if (show && !input.value) input.value = isPlugin ? "ur0:tai/" : "ux0:data/";
            input.title = isPlugin ? "Plugin file destination" : "ZIP extraction destination";
        }
        wrap.hidden = !show;
        wrap.style.display = show ? "" : "none";
        row.dataset.lastLinkType = type;
        row.querySelectorAll(".link-extract-label, .link-extract-hint").forEach(el => { el.hidden = isPlugin; });

        const pluginFields = row.querySelector(".link-plugin-fields");
        if (pluginFields) pluginFields.style.display = isPlugin ? "" : "none";
        const secSel = row.querySelector(".link-plugin-section");
        const secCustom = row.querySelector(".link-plugin-section-custom");
        if (secSel && secCustom) {
            secCustom.style.display = secSel.value === "custom" ? "" : "none";
        }
    }


    async function loadJson(path) {
        const response = await fetch(`${RAW_BASE}/${path}`);
        if (!response.ok) {
            throw new Error(`${path}: HTTP ${response.status}`);
        }
        return response.json();
    }

    async function loadData() {
        const status = $("load-status");
        try {
            const [catalog, authors, categories] = await Promise.all([
                loadJson("catalog.json"),
                loadJson("authors.json"),
                loadJson("categories.json")
            ]);

            state.catalog = Array.isArray(catalog) ? catalog : [];
            state.authors = Array.isArray(authors) ? authors : [];
            state.categories = Array.isArray(categories) ? categories : [];
            state.ready = true;

            populateAuthors();
            populateCategories();
            addLinkRow();
            setDefaultDate();
            updatePreview();

            status.textContent = `Loaded ${state.authors.length} authors, ${state.categories.length} categories and ${state.catalog.length} applications.`;
            status.classList.add("ok");
        } catch (error) {
            status.textContent = `Could not load the official catalogs: ${error.message}. You can still use the form, but category/author validation requires the catalogs.`;
            status.classList.add("error");
            addLinkRow();
            setDefaultDate();
            updatePreview();
        }
    }

    function populateAuthors() {
        const select = $("authors");
        select.innerHTML = state.authors
            .slice()
            .sort((a, b) => String(a.name || a.id).localeCompare(String(b.name || b.id)))
            .map(author => `<option value="${escapeHtml(author.id)}">${escapeHtml(author.name || author.id)} — ${escapeHtml(author.id)}</option>`)
            .join("");
        if (typeof window.refreshAuthorPicker === "function") window.refreshAuthorPicker();
    }

    function populateCategories(selectedId = "") {
        const select = $("category");
        const sorted = state.categories.slice().sort((a, b) => Number(a.order || 999) - Number(b.order || 999));
        select.innerHTML = `<option value="">Select a category...</option>` + sorted
            .map(category => `<option value="${escapeHtml(category.id)}">${escapeHtml(category.name || category.id)}</option>`)
            .join("");
        if (selectedId) {
            select.value = selectedId;
        }
        updateSubcategories();
    }

    function updateSubcategories(selected = []) {
        const category = state.categories.find(item => item.id === $("category").value);
        const select = $("subcategories");
        const picker = $("subcategory-picker");
        const subcategories = category && Array.isArray(category.subcategories) ? category.subcategories : [];
        const selectedSet = new Set((selected || []).map(String));

        select.innerHTML = subcategories.map(item => {
            const id = String(item.id);
            const isOn = selectedSet.has(id);
            return `<option value="${escapeHtml(id)}" ${isOn ? "selected" : ""}>${escapeHtml(item.name || item.id)}</option>`;
        }).join("");

        if (picker) {
            if (!subcategories.length) {
                picker.innerHTML = `<div class="subcategory-empty">${category ? "This category has no subcategories." : "Select a category first."}</div>`;
            } else {
                picker.innerHTML = subcategories.map(item => {
                    const id = String(item.id);
                    const isOn = selectedSet.has(id);
                    return `<button type="button" class="subcategory-chip${isOn ? " is-selected" : ""}" data-id="${escapeHtml(id)}" aria-pressed="${isOn ? "true" : "false"}">${escapeHtml(item.name || item.id)}</button>`;
                }).join("");
                picker.querySelectorAll(".subcategory-chip").forEach(btn => {
                    btn.addEventListener("click", () => {
                        const id = btn.getAttribute("data-id");
                        const option = [...select.options].find(item => item.value === id);
                        if (!option) return;
                        option.selected = !option.selected;
                        btn.classList.toggle("is-selected", option.selected);
                        btn.setAttribute("aria-pressed", option.selected ? "true" : "false");
                        updatePreview();
                    });
                });
            }
        }
        updatePreview();
    }

    function setDefaultDate() {
        if (!$('version_date').value) {
            $("version_date").value = today();
        }
    }

    function addScreenshotRow(value = "") {
        const list = $("screenshots-list");
        if (list.children.length >= 5) return;
        const row = document.createElement("div");
        row.className = "repeat-item screenshot-item";
        row.innerHTML = `<input class="screenshot-url" type="url" aria-label="Screenshot URL" placeholder="https://example.org/screenshot.png" value="${escapeHtml(value)}"><button type="button" class="remove-button">Remove</button>`;
        row.querySelector(".remove-button").addEventListener("click", () => { row.remove(); updatePreview(); });
        row.querySelector("input").addEventListener("input", updatePreview);
        list.appendChild(row);
        updatePreview();
    }

    function addLinkRow(value = {}) {
        const list = $("links-list");
        const row = document.createElement("div");
        row.className = "repeat-item link-item";
        row.draggable = true;
        const type = value.type || "Download";
        const name = value.name || "";
        const url = value.url || "";
        const sizeParts = parseLegacySizeField(value.size);
        let extractPath = value.extract_path || value.extractPath;
        if (extractPath === undefined || extractPath === null) {
            extractPath = (type === "Plugin") ? "ur0:tai/" : "ux0:data/";
        }
        const showExtract = linkNeedsExtractPath(type);
        const isPlugin = type === "Plugin";
        let pluginSection = value.section || "*KERNEL";
        let pluginSectionCustom = "";
        if (pluginSection !== "*main" && pluginSection !== "*KERNEL" && pluginSection !== "*ALL" && pluginSection !== "none") {
            pluginSectionCustom = pluginSection;
            pluginSection = "custom";
        }
        const pluginLine = value.line || "";
        const pluginRequirement = value.required === true ? "true" : value.required === false ? "false" : "";
        // First link defaults to recommended when not explicitly set.
        const isFirst = list.querySelectorAll(".link-item").length === 0;
        const recommended = value.recommended === true || (isFirst && value.recommended === undefined);
        row.innerHTML = `
            <span class="drag-handle" title="Drag to reorder" aria-label="Drag to reorder">⋮⋮</span>
            <select class="link-type" aria-label="Link type">${DOWNLOAD_TYPES.map(item => `<option value="${escapeHtml(item)}" ${item === type ? "selected" : ""}>${escapeHtml(item)}</option>`).join("")}</select>
            <input class="link-name" type="text" aria-label="Link name" placeholder="Name" value="${escapeHtml(name)}">
            <input class="link-url url-field" type="url" aria-label="Link URL" placeholder="https://example.org/download.vpk" value="${escapeHtml(url)}">
            <div class="size-unit-field link-size-field">
                <input class="link-size-value" type="number" aria-label="Link size" min="0" step="any" placeholder="Size" value="${escapeHtml(sizeParts.value)}">
                <select class="link-size-unit" aria-label="Size unit">${sizeUnitOptionsHtml(sizeParts.unit)}</select>
            </div>
            <div class="link-extract-wrap" ${showExtract ? "" : "hidden"} style="${showExtract ? "" : "display:none"}">
                <span class="link-extract-label">ZIP extraction destination (PS Vita)</span>
                <input class="link-extract-path" type="text" aria-label="Destination path on PS Vita" placeholder="extract_path (e.g. ux0:data/)" value="${escapeHtml(extractPath)}">
                <small class="link-extract-hint">Used only for Data Files, Game Files, Mod, Mod Pack and Patch. Example: ux0:data/</small>
            </div>
            <div class="link-plugin-fields" style="${isPlugin ? "" : "display:none;"}">
                <div class="plugin-field">
                    <span class="plugin-field-label">Config section</span>
                    <select class="link-plugin-section">
                        ${PLUGIN_SECTIONS.map(s => `<option value="${s.id}" ${s.id === pluginSection ? "selected" : ""}>${s.label}</option>`).join("")}
                    </select>
                </div>
                <div class="plugin-field plugin-field-line">
                    <span class="plugin-field-label">Config line (appended to section)</span>
                    <input class="link-plugin-line" type="text" placeholder="ur0:tai/plugin.suprx" value="${escapeHtml(pluginLine)}">
                </div>
                <div class="plugin-field">
                    <span class="plugin-field-label">Requirement</span>
                    <select class="link-plugin-required" aria-label="Plugin requirement">
                        <option value="" ${pluginRequirement === "" ? "selected" : ""}>Not specified (legacy)</option>
                        <option value="true" ${pluginRequirement === "true" ? "selected" : ""}>Required</option>
                        <option value="false" ${pluginRequirement === "false" ? "selected" : ""}>Optional</option>
                    </select>
                </div>
                <input class="link-plugin-section-custom" type="text" placeholder="Custom section e.g. *NPXS10015" value="${escapeHtml(pluginSectionCustom)}" style="${pluginSection === "custom" ? "" : "display:none;"}">
            </div>

            <label class="checkbox-inline"><input class="link-recommended" type="checkbox" ${recommended ? "checked" : ""}> Recommended</label>
            <button type="button" class="remove-button">Remove</button>`;
        row.querySelectorAll("input,select").forEach(element => element.addEventListener("input", updatePreview));
        const typeSelect = row.querySelector(".link-type");
        if (typeSelect) {
            typeSelect.addEventListener("change", () => {
                syncLinkExtractPathVisibility(row);
                updatePreview();
            });
        }
        const secSel = row.querySelector(".link-plugin-section");
        if (secSel) {
            secSel.addEventListener("change", () => {
                syncLinkExtractPathVisibility(row);
                updatePreview();
            });
        }
        syncLinkExtractPathVisibility(row);
        const recInput = row.querySelector(".link-recommended");
        if (recInput) {
            recInput.addEventListener("change", () => {
                if (recInput.checked) {
                    document.querySelectorAll(".link-recommended").forEach(cb => {
                        if (cb !== recInput) cb.checked = false;
                    });
                }
                updatePreview();
            });
        }
        row.querySelector(".remove-button").addEventListener("click", () => { row.remove(); updatePreview(); });
        bindLinkDrag(row);
        list.appendChild(row);
        updatePreview();
    }

    function bindLinkDrag(row) {
        row.addEventListener("dragstart", event => {
            const t = event.target;
            if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA" || t.tagName === "BUTTON" || t.closest("label"))) {
                event.preventDefault();
                return;
            }
            row.classList.add("dragging");
            event.dataTransfer.effectAllowed = "move";
            try { event.dataTransfer.setData("text/plain", "link"); } catch (_) {}
        });
        row.addEventListener("dragend", () => {
            row.classList.remove("dragging");
            document.querySelectorAll(".link-item.drag-over").forEach(el => el.classList.remove("drag-over"));
            updatePreview();
        });
        row.addEventListener("dragover", event => {
            event.preventDefault();
            event.dataTransfer.dropEffect = "move";
            const dragging = document.querySelector(".link-item.dragging");
            if (!dragging || dragging === row) return;
            row.classList.add("drag-over");
            const list = $("links-list");
            const siblings = [...list.querySelectorAll(".link-item")];
            const from = siblings.indexOf(dragging);
            const to = siblings.indexOf(row);
            if (from < 0 || to < 0 || from === to) return;
            if (from < to) list.insertBefore(dragging, row.nextSibling);
            else list.insertBefore(dragging, row);
        });
        row.addEventListener("dragleave", () => row.classList.remove("drag-over"));
        row.addEventListener("drop", event => {
            event.preventDefault();
            row.classList.remove("drag-over");
            updatePreview();
        });
    }

    function getSelectedValues(select) {
        return [...select.selectedOptions].map(option => option.value);
    }

    function optionalString(id) {
        const value = $(id).value.trim();
        return value || undefined;
    }

    function optionalNumber(id) {
        const raw = $(id).value.trim();
        if (!raw) return undefined;
        const number = Number(raw);
        return Number.isFinite(number) ? number : undefined;
    }

    function collectLinks() {
        return [...document.querySelectorAll(".link-item")].map(row => {
            const result = {
                type: row.querySelector(".link-type").value,
                name: row.querySelector(".link-name").value.trim(),
                url: row.querySelector(".link-url").value.trim()
            };
            const sizeValEl = row.querySelector(".link-size-value");
            const sizeUnitEl = row.querySelector(".link-size-unit");
            const sizeBytes = parseSizeToBytes(
                sizeValEl ? sizeValEl.value : "",
                sizeUnitEl ? sizeUnitEl.value : "MB"
            );
            if (sizeBytes !== null && sizeBytes > 0) result.size = sizeBytes;
            if (linkNeedsExtractPath(result.type) || result.type === "Plugin") {
                const pathEl = row.querySelector(".link-extract-path");
                const extractPath = pathEl ? pathEl.value.trim() : "";
                if (extractPath) result.extract_path = extractPath;
            }
            if (result.type === "Plugin") {
                if (!result.extract_path) result.extract_path = "ur0:tai/";
                const secSel = row.querySelector(".link-plugin-section");
                const secCustom = row.querySelector(".link-plugin-section-custom");
                let section = secSel ? secSel.value : "none";
                if (section === "custom") {
                    section = secCustom ? secCustom.value.trim() : "";
                }
                if (section) result.section = section;
                const lineEl = row.querySelector(".link-plugin-line");
                const line = lineEl ? lineEl.value.trim() : "";
                if (line) result.line = line;
                const requirementChoice = row.querySelector(".link-plugin-required").value;
                if (requirementChoice !== "") result.required = requirementChoice === "true";
            }
            if (row.querySelector(".link-recommended").checked) result.recommended = true;
            return result;
        });
    }

    function collectApp() {
        const app = {
            id: $("id").value.trim(),
            title_id: $("title_id").value.trim(),
            name: $("name").value.trim(),
            description: $("description").value.trim(),
            long_description: $("long_description").value.trim(),
            author_ids: getSelectedValues($("authors")),
            category_id: $("category").value,
            subcategory_ids: getSelectedValues($("subcategories")),
            version: $("version").value.trim(),
            version_date: $("version_date").value,
            requirements: $("requirements").value.trim(),
            size: parseSizeToBytes($("size").value, $("size_unit").value) || 0,
            status: $("status").value,
            links: collectLinks()
        };

        const icon = optionalString("icon");
        if (icon) app.icon = icon;

        const screenshots = [...document.querySelectorAll(".screenshot-url")]
            .map(input => input.value.trim())
            .filter(Boolean);
        if (screenshots.length) app.screenshots = screenshots;

        const optionalFields = [
            ["source_name", "source_name"],
            ["source_id", "source_id"],
            ["source_url", "source_url"],
            ["release_page", "release_page"],
            ["changelog", "changelog"],
            ["data_url", "data_url"],
            ["updated_at", "updated_at"]
        ];
        optionalFields.forEach(([id, key]) => {
            const value = optionalString(id);
            if (value !== undefined) app[key] = value;
        });

        [
            ["downloads", "downloads"],
            ["data_size", "data_size"],
            ["score", "score"]
        ].forEach(([id, key]) => {
            const value = optionalNumber(id);
            if (value !== undefined) app[key] = value;
        });

        return app;
    }

    function validUrl(value) {
        try {
            const url = new URL(value);
            return url.protocol === "http:" || url.protocol === "https:";
        } catch {
            return false;
        }
    }

    function validateApp(app) {
        const errors = [];
        const add = (message, target) => errors.push({ message, target });
        const labels = {
            id: "Internal ID", title_id: "Title ID", name: "Name",
            description: "Short description", long_description: "Long description",
            version: "Version", version_date: "Version date",
            requirements: "Requirements", status: "Status"
        };

        Object.keys(labels).forEach(key => {
            if (!String(app[key] || "").trim()) {
                add(labels[key] + " is required.", key === "id" && state.mode === "create" ? "name" : key);
            }
        });
        if (app.id && !/^[a-z0-9_-]+$/.test(app.id)) add("Internal ID may only use lowercase letters, numbers, hyphens and underscores.", "id");
        if (app.id && state.mode === "create" && state.catalog.some(item => item.id === app.id)) {
            add("Internal ID '" + app.id + "' already exists. Use Edit mode for an existing entry.", "name");
        }
        if (app.title_id && state.mode === "create" && state.catalog.some(item => item.title_id === app.title_id)) {
            add("Title ID '" + app.title_id + "' already exists in the catalog.", "title_id");
        }

        if (!Array.isArray(app.author_ids) || !app.author_ids.length) add("Authors: select at least one author.", "authors");
        if (state.ready) {
            const isLocalAuthor = id => typeof window.isCreatedAuthor === "function" && window.isCreatedAuthor(id);
            const missing = app.author_ids.filter(id => !state.authors.some(a => a.id === id) && !isLocalAuthor(id));
            if (missing.length) add("Authors: missing profiles for " + missing.join(", ") + ".", "authors");
            if (!app.category_id || !state.categories.some(c => c.id === app.category_id)) {
                add("Category: choose an official category.", "category");
            }
            const category = state.categories.find(c => c.id === app.category_id);
            const allowed = new Set((category?.subcategories || []).map(c => c.id));
            if (!app.subcategory_ids.length) add("Subcategories: select at least one.", "subcategories");
            app.subcategory_ids.forEach(id => {
                if (!allowed.has(id)) add("Subcategory '" + id + "' does not belong to this category.", "subcategories");
            });
        } else if (!app.category_id) {
            add("Category: select a category.", "category");
        }

        if (!/^\d{4}-\d{2}-\d{2}$/.test(app.version_date)) add("Version date must use YYYY-MM-DD.", "version_date");
        if (!Number.isSafeInteger(app.size) || app.size <= 0) {
            add("File size is missing or invalid. Enter the VPK size and select B, KB, MB or GB.", "size");
        }
        if (!["Verified", "Legacy", "Archive"].includes(app.status)) add("Choose a valid status.", "status");

        const rows = [...document.querySelectorAll(".link-item")];
        if (!app.links.length) add("Links: add at least one link.", "links-list");
        let recommended = 0;
        app.links.forEach((link, i) => {
            const prefix = "link:" + i + ":";
            if (!link.type) add("Link " + (i + 1) + ": select a type.", prefix + "type");
            if (!link.name) add("Link " + (i + 1) + ": enter a name.", prefix + "name");
            if (!validUrl(link.url)) add("Link " + (i + 1) + ": enter a valid http:// or https:// URL.", prefix + "url");
            if (Object.prototype.hasOwnProperty.call(link, "required") &&
                (link.type !== "Plugin" || typeof link.required !== "boolean")) {
                add("Link " + (i + 1) + ": the required flag is only allowed on Plugin links.", prefix + "type");
            }
            const sizeInput = rows[i]?.querySelector(".link-size-value");
            const unitInput = rows[i]?.querySelector(".link-size-unit");
            if (sizeInput && sizeInput.value.trim()) {
                const bytes = parseSizeToBytes(sizeInput.value, unitInput?.value);
                if (!Number.isSafeInteger(bytes) || bytes <= 0) {
                    add("Link " + (i + 1) + ": size must be greater than zero (choose the correct unit).", prefix + "size");
                }
            }
            if (link.recommended) recommended++;
        });
        if (recommended > 1) add("Links: only one link may be Recommended.", "links-list");

        if (app.icon && !validUrl(app.icon)) add("Icon URL must start with http:// or https://.", "icon");
        if (app.screenshots && (app.screenshots.length < 1 || app.screenshots.length > 5)) {
            add("Screenshots: provide between 1 and 5 images.", "screenshots-list");
        }
        [...document.querySelectorAll(".screenshot-url")].forEach((input, i) => {
            const url = input.value.trim();
            if (url && !validUrl(url)) add("Screenshot " + (i + 1) + ": enter a valid URL.", "screenshot:" + i);
        });
        ["source_url", "release_page", "data_url"].forEach(key => {
            if (app[key] && !validUrl(app[key])) add(key + ": enter a valid http:// or https:// URL.", key);
        });
        return errors;
    }

    function issueElement(target) {
        if (target === "authors") return $("author-search") || $("author-picker") || $("authors");
        if (target === "subcategories") return $("subcategory-picker");
        const link = /^link:(\d+):(type|name|url|size)$/.exec(target);
        if (link) {
            const row = document.querySelectorAll(".link-item")[Number(link[1])];
            const selectors = {
                type: ".link-type", name: ".link-name", url: ".link-url", size: ".link-size-value"
            };
            return row?.querySelector(selectors[link[2]]) || row || null;
        }
        const screenshot = /^screenshot:(\d+)$/.exec(target);
        if (screenshot) return document.querySelectorAll(".screenshot-url")[Number(screenshot[1])] || $("screenshots-list");
        return $(target);
    }

    function navigateToIssue(target) {
        const element = issueElement(target);
        if (!element) return;
        const details = element.closest("details");
        if (details) details.open = true;
        const area = element.closest(".repeat-item, .field, .subcategory-block") || element;
        area.scrollIntoView({ behavior: window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ? "auto" : "smooth", block: "center" });
        const control = element.matches("input, select, textarea, button")
            ? element : element.querySelector("input:not([type=hidden]), button, select, textarea");
        if (control && !control.hidden && control.getAttribute("aria-hidden") !== "true") {
            control.focus({ preventScroll: true });
        }
    }

    function clearIssueHighlights() {
        document.querySelectorAll("#app-form .is-invalid-field").forEach(el => el.classList.remove("is-invalid-field"));
        document.querySelectorAll("#app-form [aria-invalid='true']").forEach(el => el.removeAttribute("aria-invalid"));
        document.querySelectorAll("#app-form .form-card.has-errors").forEach(el => el.classList.remove("has-errors"));
    }

    function getCreatedAuthors() {
        return typeof window.getCreatedAuthorProfiles === "function" ? window.getCreatedAuthorProfiles() : [];
    }

    function updateDownloadButton() {
        const button = $("download-json");
        if (!button) return;
        const count = getCreatedAuthors().length;
        const base = state.mode === "edit" ? "Download updated app.json" : "Download app.json";
        button.textContent = count ? `${base} + ${count} author JSON${count === 1 ? "" : "s"}` : base;
    }

    function updateValidation(app) {
        const box = $("validation-summary");
        const errors = validateApp(app);
        updateDownloadButton();
        clearIssueHighlights();
        if (!errors.length) {
            box.className = "validation-summary valid";
            box.innerHTML = "<p><strong>✓ Ready to download.</strong> Client-side checks passed. GitHub Actions will perform the final repository validation.</p>";
            $("download-json").disabled = false;
        } else {
            box.className = "validation-summary invalid";
            box.innerHTML = "";
            const heading = document.createElement("strong");
            heading.textContent = errors.length + " issue" + (errors.length === 1 ? "" : "s") + " to fix before downloading";
            const hint = document.createElement("p");
            hint.textContent = "Select any issue to jump directly to the field that needs attention.";
            const list = document.createElement("ul");
            errors.forEach(error => {
                const item = document.createElement("li");
                const button = document.createElement("button");
                button.type = "button";
                button.className = "validation-jump";
                button.dataset.target = error.target;
                button.textContent = error.message;
                item.appendChild(button);
                list.appendChild(item);
                const element = issueElement(error.target);
                if (element) {
                    element.classList.add("is-invalid-field");
                    if (element.matches("input, textarea, select")) element.setAttribute("aria-invalid", "true");
                    const card = element.closest(".form-card");
                    if (card) card.classList.add("has-errors");
                }
            });
            box.append(heading, hint, list);
            $("download-json").disabled = true;
        }
        return errors;
    }

    function updatePreview() {
        syncIdFromName();
        const app = collectApp();
        const status = $("download-status");
        if (status && state.lastDownloadedSnapshot && JSON.stringify(app) !== state.lastDownloadedSnapshot) status.hidden = true;
        $("json-preview").textContent = JSON.stringify(app, null, 2);
        updateValidation(app);
    }

    function resetForm() {
        if (typeof window.clearCreatedAuthors === "function") window.clearCreatedAuthors();
        $("app-form").reset();
        state.lastDownloadedSnapshot = null;
        const downloadStatus = $("download-status");
        if (downloadStatus) { downloadStatus.hidden = true; downloadStatus.textContent = ""; }
        $("screenshots-list").innerHTML = "";
        $("links-list").innerHTML = "";
        $("category").value = "";
        updateSubcategories();
        document.querySelectorAll(".screenshot-url").forEach(input => input.remove());
        addLinkRow();
        setDefaultDate();
        if (typeof window.refreshAuthorPicker === "function") window.refreshAuthorPicker();
        updatePreview();
    }

    function populateFromObject(app) {
        resetForm();
        $("id").value = app.id || "";
        $("title_id").value = app.title_id || "";
        $("name").value = app.name || "";
        $("description").value = app.description || "";
        $("long_description").value = app.long_description || "";
        $("version").value = app.version || "";
        $("version_date").value = app.version_date || "";
        $("requirements").value = app.requirements || "";
        {
            const parts = parseLegacySizeField(app.size);
            $("size").value = parts.value;
            if ($("size_unit")) $("size_unit").value = parts.unit;
        }
        $("status").value = app.status || "Verified";
        $("icon").value = app.icon || "";
        $("release_page").value = app.release_page || "";
        $("source_url").value = app.source_url || "";
        $("source_name").value = app.source_name || "";
        $("source_id").value = app.source_id || "";
        $("changelog").value = app.changelog || "";
        $("downloads").value = app.downloads ?? "";
        $("data_size").value = app.data_size ?? "";
        $("data_url").value = app.data_url || "";
        $("score").value = app.score ?? "";
        $("updated_at").value = app.updated_at ? String(app.updated_at).slice(0, 16) : "";

        [...$("authors").options].forEach(option => { option.selected = Array.isArray(app.author_ids) && app.author_ids.includes(option.value); });
        populateCategories(app.category_id || "");
        updateSubcategories(Array.isArray(app.subcategory_ids) ? app.subcategory_ids : []);

        (app.screenshots || []).forEach(addScreenshotRow);
        $("links-list").innerHTML = "";
        (app.links || []).forEach(addLinkRow);
        if (!app.links || !app.links.length) addLinkRow();
        if (typeof window.refreshAuthorPicker === "function") window.refreshAuthorPicker();
        updatePreview();
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

    function downloadJson() {
        const app = collectApp();
        if (updateValidation(app).length) { navigateToIssue(validateApp(app)[0].target); return; }
        const authors = getCreatedAuthors();
        authors.forEach(author => downloadBlob(`${author.id}.json`, author));
        downloadBlob(`${app.id}.json`, app);
        state.lastDownloadedSnapshot = JSON.stringify(app);
        const status = $("download-status");
        if (status) {
            status.hidden = false;
            status.textContent = "JSON prepared for download: " + app.id + ".json" + (authors.length ? " (plus " + authors.length + " author JSON file(s))." : ".") + " Add the files to apps/ and authors/ as appropriate; GitHub Actions validates them after submission.";
        }
    }

    async function copyJson() {
        const text = $("json-preview").textContent;
        try {
            await navigator.clipboard.writeText(text);
            $("copy-json").textContent = "Copied!";
            setTimeout(() => { $("copy-json").textContent = "Copy JSON"; }, 1200);
        } catch {
            alert("Clipboard access is unavailable. Select the preview and copy it manually.");
        }
    }


    function startNewApp() {
        const draft = collectApp();
        const hasData = Boolean(draft.name || draft.title_id || draft.description ||
            draft.long_description || draft.requirements ||
            draft.links.some(link => link.name || link.url) || getCreatedAuthors().length);
        if (hasData && state.lastDownloadedSnapshot !== JSON.stringify(draft) &&
            !window.confirm("Create a new JSON? Unsaved changes to this form will be lost.")) return;
        setMode("create");
        resetForm();
        $("name").scrollIntoView({ behavior: "smooth", block: "center" });
        $("name").focus({ preventScroll: true });
    }

    function setMode(mode) {
        state.mode = mode === "edit" ? "edit" : "create";
        const createBtn = $("mode-create");
        const editBtn = $("mode-edit");
        const hint = $("mode-hint");
        if (createBtn) createBtn.classList.toggle("active", state.mode === "create");
        if (editBtn) editBtn.classList.toggle("active", state.mode === "edit");
        if (hint) {
            if (state.mode === "edit") {
                hint.className = "notice info";
                hint.textContent = "Edit mode: import an existing app JSON, change fields, and download the updated file. Duplicate ID / Title ID checks are skipped so you can save over the same entry. Internal ID is editable.";
            } else {
                hint.className = "notice info";
                hint.textContent = "Create mode: Internal ID is generated from the Name (not editable). Title ID must be new in the catalog.";
            }
        }
        applyIdFieldMode();
        updatePreview();
    }

    function bind() {
        document.querySelectorAll("#app-form input, #app-form textarea, #app-form select").forEach(element => {
            element.addEventListener("input", updatePreview);
            element.addEventListener("change", updatePreview);
        });
        $("category").addEventListener("change", () => updateSubcategories());
        $("add-screenshot").addEventListener("click", () => addScreenshotRow());
        $("add-link").addEventListener("click", () => addLinkRow());
        $("reset-form").addEventListener("click", startNewApp);
        $("create-another").addEventListener("click", startNewApp);
        $("validation-summary").addEventListener("click", event => {
            const button = event.target.closest(".validation-jump");
            if (button) navigateToIssue(button.dataset.target);
        });
        $("download-json").addEventListener("click", downloadJson);
        $("copy-json").addEventListener("click", copyJson);
        if ($("mode-create")) $("mode-create").addEventListener("click", () => setMode("create"));
        if ($("mode-edit")) $("mode-edit").addEventListener("click", () => setMode("edit"));
        $("import-json").addEventListener("change", async event => {
            const file = event.target.files?.[0];
            if (!file) return;
            try {
                const object = JSON.parse(await file.text());
                if (!object || typeof object !== "object" || Array.isArray(object)) throw new Error("Root value must be an object.");
                setMode("edit");
                populateFromObject(object);
            } catch (error) {
                alert(`Could not import JSON: ${error.message}`);
            }
            event.target.value = "";
        });
    }

    bind();
    setMode("create");
    loadData();
})();
