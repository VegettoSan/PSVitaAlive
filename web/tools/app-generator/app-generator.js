(() => {
    "use strict";

    const RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main";
    const DOWNLOAD_TYPES = [
        "Download",
        "Mirror",
        "Repository",
        "Official Website",
        "Documentation",
        "Issues",
        "Community",
        "Other"
    ];

    const state = {
        catalog: [],
        authors: [],
        categories: [],
        ready: false
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
        const subcategories = category && Array.isArray(category.subcategories) ? category.subcategories : [];
        select.innerHTML = subcategories.map(item => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name || item.id)}</option>`).join("");
        selected.forEach(id => {
            const option = [...select.options].find(item => item.value === id);
            if (option) option.selected = true;
        });
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
        row.innerHTML = `<input class="screenshot-url" type="url" placeholder="https://example.org/screenshot.png" value="${escapeHtml(value)}"><button type="button" class="remove-button">Remove</button>`;
        row.querySelector("remove-button");
        row.querySelector(".remove-button").addEventListener("click", () => { row.remove(); updatePreview(); });
        row.querySelector("input").addEventListener("input", updatePreview);
        list.appendChild(row);
        updatePreview();
    }

    function addLinkRow(value = {}) {
        const list = $("links-list");
        const row = document.createElement("div");
        row.className = "repeat-item link-item";
        const type = value.type || "Download";
        const name = value.name || "";
        const url = value.url || "";
        const recommended = value.recommended === true;
        row.innerHTML = `
            <select class="link-type">${DOWNLOAD_TYPES.map(item => `<option value="${escapeHtml(item)}" ${item === type ? "selected" : ""}>${escapeHtml(item)}</option>`).join("")}</select>
            <input class="link-name" type="text" placeholder="Name" value="${escapeHtml(name)}">
            <input class="link-url url-field" type="url" placeholder="https://example.org/download.vpk" value="${escapeHtml(url)}">
            <label class="checkbox-inline"><input class="link-recommended" type="checkbox" ${recommended ? "checked" : ""}> Recommended</label>
            <button type="button" class="remove-button">Remove</button>`;
        row.querySelectorAll("input,select").forEach(element => element.addEventListener("input", updatePreview));
        row.querySelector(".remove-button").addEventListener("click", () => { row.remove(); updatePreview(); });
        list.appendChild(row);
        updatePreview();
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
            size: Number($("size").value),
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
            ["source_name", "source_name", "string"],
            ["source_id", "source_id", "string"],
            ["source_url", "source_url", "string"],
            ["release_page", "release_page", "string"],
            ["changelog", "changelog", "string"],
            ["data_url", "data_url", "string"],
            ["updated_at", "updated_at", "string"]
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
        const requiredStrings = ["id", "title_id", "name", "description", "long_description", "version", "version_date", "requirements", "status"];
        requiredStrings.forEach(key => {
            if (!String(app[key] || "").trim()) errors.push(`${key} is required.`);
        });

        if (app.id && !/^[a-z0-9_-]+$/.test(app.id)) errors.push("id may only contain lowercase letters, numbers, hyphens and underscores.");
        if (app.id && state.catalog.some(item => item.id === app.id)) errors.push(`The ID '${app.id}' already exists in catalog.json.`);
        if (app.title_id && state.catalog.some(item => item.title_id === app.title_id)) errors.push(`The Title ID '${app.title_id}' already exists in catalog.json.`);
        if (!Array.isArray(app.author_ids) || !app.author_ids.length) errors.push("Select at least one author.");
        if (state.ready) {
            const missingAuthors = app.author_ids.filter(id => !state.authors.some(author => author.id === id));
            if (missingAuthors.length) errors.push(`Author profiles missing from authors/: ${missingAuthors.join(", ")}.`);
            if (!app.category_id || !state.categories.some(category => category.id === app.category_id)) errors.push("Select an official category.");
            const category = state.categories.find(item => item.id === app.category_id);
            const allowed = new Set((category?.subcategories || []).map(item => item.id));
            if (!app.subcategory_ids.length) errors.push("Select at least one subcategory.");
            app.subcategory_ids.forEach(id => { if (!allowed.has(id)) errors.push(`Subcategory '${id}' does not belong to the selected category.`); });
        } else if (!app.category_id) {
            errors.push("Select a category.");
        }

        if (!/^\d{4}-\d{2}-\d{2}$/.test(app.version_date)) errors.push("version_date must use YYYY-MM-DD.");
        if (!Number.isInteger(app.size) || app.size <= 0) errors.push("size must be a positive integer in bytes.");
        if (!["Verified", "Legacy", "Archive"].includes(app.status)) errors.push("status must be Verified, Legacy or Archive.");
        if (!Array.isArray(app.links) || !app.links.length) errors.push("At least one link is required.");
        let recommended = 0;
        app.links.forEach((link, index) => {
            if (!link.type) errors.push(`Link ${index + 1}: type is required.`);
            if (!link.name) errors.push(`Link ${index + 1}: name is required.`);
            if (!validUrl(link.url)) errors.push(`Link ${index + 1}: URL must use http:// or https://.`);
            if (link.recommended) recommended++;
        });
        if (recommended > 1) errors.push("At most one link may be recommended.");

        if (app.icon && !validUrl(app.icon)) errors.push("Icon must be an http:// or https:// URL.");
        if (app.screenshots && (app.screenshots.length < 1 || app.screenshots.length > 5)) errors.push("Screenshots must contain between 1 and 5 images.");
        (app.screenshots || []).forEach((url, index) => { if (!validUrl(url)) errors.push(`Screenshot ${index + 1}: URL must use http:// or https://.`); });
        ["source_url", "release_page", "data_url"].forEach(key => { if (app[key] && !validUrl(app[key])) errors.push(`${key} must be an http:// or https:// URL.`); });

        return errors;
    }

    function updateValidation(app) {
        const box = $("validation-summary");
        const errors = validateApp(app);
        if (!errors.length) {
            box.className = "validation-summary valid";
            box.innerHTML = "<p><strong>✓ Ready.</strong> The generated object satisfies the client-side rules used by the current catalog validator.</p>";
            $("download-json").disabled = false;
        } else {
            box.className = "validation-summary invalid";
            box.innerHTML = `<strong>Fix ${errors.length} issue${errors.length === 1 ? "" : "s"} before downloading.</strong><ul>${errors.map(error => `<li>${escapeHtml(error)}</li>`).join("")}</ul>`;
            $("download-json").disabled = true;
        }
        return errors;
    }

    function updatePreview() {
        const app = collectApp();
        $("json-preview").textContent = JSON.stringify(app, null, 2);
        updateValidation(app);
    }

    function resetForm() {
        $("app-form").reset();
        $("screenshots-list").innerHTML = "";
        $("links-list").innerHTML = "";
        $("category").value = "";
        updateSubcategories();
        document.querySelectorAll(".screenshot-url").forEach(input => input.remove());
        addLinkRow();
        setDefaultDate();
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
        $("size").value = Number.isFinite(Number(app.size)) ? app.size : "";
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
        updatePreview();
    }

    function downloadJson() {
        const app = collectApp();
        if (validateApp(app).length) return;
        const blob = new Blob([JSON.stringify(app, null, 2) + "\n"], { type: "application/json;charset=utf-8" });
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = `${app.id}.json`;
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        URL.revokeObjectURL(url);
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

    function bind() {
        document.querySelectorAll("#app-form input, #app-form textarea, #app-form select").forEach(element => element.addEventListener("input", updatePreview));
        $("category").addEventListener("change", () => updateSubcategories());
        $("add-screenshot").addEventListener("click", () => addScreenshotRow());
        $("add-link").addEventListener("click", () => addLinkRow());
        $("reset-form").addEventListener("click", resetForm);
        $("download-json").addEventListener("click", downloadJson);
        $("copy-json").addEventListener("click", copyJson);
        $("import-json").addEventListener("change", async event => {
            const file = event.target.files?.[0];
            if (!file) return;
            try {
                const object = JSON.parse(await file.text());
                if (!object || typeof object !== "object" || Array.isArray(object)) throw new Error("Root value must be an object.");
                populateFromObject(object);
            } catch (error) {
                alert(`Could not import JSON: ${error.message}`);
            }
            event.target.value = "";
        });
    }

    bind();
    loadData();
})();
