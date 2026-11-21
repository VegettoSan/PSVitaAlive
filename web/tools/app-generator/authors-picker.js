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
    let authorDialogPreviousFocus = null;

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

    const HOSTED_PROFILE_DOMAINS = new Set(["github.com", "gitlab.com"]);
    const RESERVED_PATHS = {
        "github.com": new Set(["about", "apps", "collections", "contact", "enterprise",
            "events", "explore", "features", "issues", "join", "login", "marketplace",
            "new", "notifications", "orgs", "organizations", "pricing", "pulls",
            "search", "settings", "site", "sponsors", "topics", "trending"]),
        "gitlab.com": new Set(["-", "admin", "dashboard", "explore", "groups", "help",
            "issues", "login", "merge_requests", "oauth", "profile", "projects",
            "public", "search", "snippets", "users"])
    };

    function parseAuthorSearchLink(value) {
        const raw = String(value || "").trim();
        if (!/^(?:https?:\/\/)?(?:www\.)?(?:github|gitlab)\.com(?:[\/:?#]|$)/i.test(raw)) return null;
        let url;
        try {
            url = new URL(/^https?:\/\//i.test(raw) ? raw : "https://" + raw);
        } catch {
            return { error: "Invalid GitHub or GitLab URL." };
        }
        const domain = url.hostname.toLowerCase().replace(/^www\./, "");
        if (!HOSTED_PROFILE_DOMAINS.has(domain) ||
            !["http:", "https:"].includes(url.protocol) ||
            url.username || url.password || url.port) {
            return { error: "Enter a valid public GitHub or GitLab account or repository URL." };
        }
        const segments = url.pathname.split("/").filter(Boolean);
        if (!segments.length) return { error: "Include a username in the profile or repository URL." };
        let username;
        try { username = decodeURIComponent(segments[0]); }
        catch { return { error: "The URL contains an invalid username." }; }
        if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/.test(username) ||
            RESERVED_PATHS[domain].has(username.toLowerCase()) ||
            (domain === "github.com" &&
                !/^[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,37}[a-zA-Z0-9])?$/.test(username))) {
            return { error: "This link is not a supported developer account URL." };
        }
        const provider = domain === "github.com" ? "GitHub" : "GitLab";
        const profileUrl = `https://${domain}/${encodeURIComponent(username)}`;
        let repositoryUrl = "";
        if (segments.length > 1) {
            if (domain === "github.com") {
                repositoryUrl = `https://github.com/${encodeURIComponent(username)}/${encodeURIComponent(segments[1].replace(/\.git$/i, ""))}`;
            } else {
                const cleanPath = segments.slice(0, Math.max(1, segments.indexOf("-") < 0 ?
                    segments.length : segments.indexOf("-"))).join("/").replace(/\.git$/i, "");
                repositoryUrl = `https://gitlab.com/${cleanPath}`;
            }
        }
        return { provider, username, profileUrl, repositoryUrl,
            enteredUrl: `https://${domain}${url.pathname.replace(/\/$/, "")}` };
    }

    function authorProfileMetadata() {
        const profiles = new Map();
        const published = typeof window.getGeneratorAuthorProfiles === "function"
            ? window.getGeneratorAuthorProfiles() : [];
        for (const author of published || []) {
            if (author && author.id) profiles.set(author.id, author);
        }
        for (const author of importedAuthors.values()) profiles.set(author.id, author);
        for (const author of createdAuthors.values()) profiles.set(author.id, author);
        return profiles;
    }

    function normalizeHostedLink(value) {
        const parsed = parseAuthorSearchLink(value);
        if (parsed && !parsed.error) {
            try {
                const url = new URL(/^https?:\/\//i.test(value) ? value : "https://" + value);
                return url.hostname.toLowerCase().replace(/^www\./, "") +
                    url.pathname.replace(/\.git\/?$/i, "").replace(/\/+$/, "").toLowerCase();
            } catch { return ""; }
        }
        return "";
    }

    function authorUsernameIdMatches(option, source) {
        // The username in the URL is the primary author identifier in VitaAlive.
        // Prefer a case-insensitive exact ID before trying the generator's normalized slug.
        const username = source.username.toLowerCase();
        const id = option.value.toLowerCase();
        if (id === username) {
            return { option, rank: 0, reason: "Username matches ID" };
        }
        if (id === slugify(source.username)) {
            return { option, rank: 1, reason: "Username matches normalized ID" };
        }
        return null;
    }

    function authorLinkMatches(option, source, profiles) {
        const author = profiles.get(option.value);
        const authorLinks = Array.isArray(author?.links) ? author.links : [];
        const exact = normalizeHostedLink(source.enteredUrl);
        for (const link of authorLinks) {
            if (!link || typeof link.url !== "string") continue;
            const normalized = normalizeHostedLink(link.url);
            if (normalized && (normalized === exact ||
                (source.repositoryUrl && normalized === normalizeHostedLink(source.repositoryUrl)))) {
                return { option, rank: 0, reason: "Same link" };
            }
        }
        // Compare only URLs already attributed to the author, not unrelated content.
        for (const link of authorLinks) {
            const parsed = parseAuthorSearchLink(link?.url);
            if (parsed && !parsed.error && parsed.provider === source.provider &&
                parsed.username.toLowerCase() === source.username.toLowerCase()) {
                return { option, rank: 1, reason: "Same account" };
            }
        }
        return null;
    }

    function authorNameMatches(option, source) {
        const name = String(option.textContent || "").split(" — ")[0].trim();
        if (slugify(name) === slugify(source.username)) {
            return { option, rank: 0, reason: "Matching name" };
        }
        return null;
    }

    // Similarity is advisory only; never select or merge authors automatically.
    function authorSearchKey(value) {
        return slugify(value || "").replace(/-/g, "");
    }

    function editDistanceWithTransposition(a, b) {
        // Optimal string alignment: insertions, deletions, substitutions and adjacent swaps.
        let previousPrevious = null;
        let previous = Array.from({ length: b.length + 1 }, (_, i) => i);
        for (let i = 1; i <= a.length; i++) {
            const current = [i];
            for (let j = 1; j <= b.length; j++) {
                const cost = a[i - 1] === b[j - 1] ? 0 : 1;
                let distance = Math.min(previous[j] + 1,
                    current[j - 1] + 1, previous[j - 1] + cost);
                if (i > 1 && j > 1 &&
                    a[i - 1] === b[j - 2] && a[i - 2] === b[j - 1]) {
                    distance = Math.min(distance, previousPrevious[j - 2] + 1);
                }
                current[j] = distance;
            }
            previousPrevious = previous;
            previous = current;
        }
        return previous[b.length];
    }

    function possibleAuthorTypo(query, candidate) {
        const a = authorSearchKey(query);
        const b = authorSearchKey(candidate);
        // Very short queries produce too many false positives.
        if (a.length < 4 || b.length < 4) return null;
        const maxDistance = a.length <= 4 ? 1 : a.length <= 8 ? 2 : 3;
        if (Math.abs(a.length - b.length) > maxDistance) return null;
        const distance = editDistanceWithTransposition(a, b);
        // Identical letters with different separators are also worth suggesting.
        if (distance > maxDistance ||
            distance / Math.max(a.length, b.length) > 0.35) return null;
        return distance;
    }

    function fuzzyAuthorMatches(query, options, profiles, provider = "") {
        const matches = [];
        for (const option of options) {
            const name = String(option.textContent || "").split(" — ")[0].trim();
            const candidates = [
                { value: option.value, reason: "Similar ID", rank: 0 },
                { value: name, reason: "Similar name", rank: 1 }
            ];
            if (provider) {
                const author = profiles.get(option.value);
                for (const link of Array.isArray(author?.links) ? author.links : []) {
                    const account = parseAuthorSearchLink(link?.url);
                    if (account && !account.error && account.provider === provider) {
                        candidates.push({ value: account.username, reason: "Similar linked account", rank: 2 });
                    }
                }
            }
            let closest = null;
            for (const candidate of candidates) {
                const distance = possibleAuthorTypo(query, candidate.value);
                if (distance === null) continue;
                if (!closest || distance < closest.distance ||
                    (distance === closest.distance && candidate.rank < closest.rank)) {
                    closest = { option, reason: candidate.reason, rank: candidate.rank,
                        distance, similar: true };
                }
            }
            if (closest) matches.push(closest);
        }
        return matches.sort((a, b) => a.distance - b.distance ||
            a.rank - b.rank || a.option.textContent.localeCompare(b.option.textContent)).slice(0, 8);
    }

    function findExistingAuthorsFromText(query) {
        const needle = query.trim().toLocaleLowerCase();
        const normalized = normalizeAuthorName(query);
        const matches = getOptions().map(option => {
            const id = option.value.toLocaleLowerCase();
            const name = String(option.textContent || "").split(" — ")[0].trim();
            const lowerName = name.toLocaleLowerCase();
            if (id === needle) return { option, rank: 0, reason: "Exact ID" };
            if (normalizeAuthorName(name) === normalized) return { option, rank: 1, reason: "Exact name" };
            if (id.includes(needle)) return { option, rank: 2, reason: "" };
            if (lowerName.includes(needle)) return { option, rank: 3, reason: "" };
            return null;
        }).filter(Boolean);
        if (matches.length) {
            return matches.sort((a, b) => a.rank - b.rank ||
                a.option.textContent.localeCompare(b.option.textContent));
        }
        return fuzzyAuthorMatches(query, getOptions(), authorProfileMetadata());
    }

    function findExistingAuthorsFromUrl(source) {
        const options = getOptions();

        // Run fallback searches ONLY when no username-to-ID match exists.
        // This avoids unrelated saved links outranking the canonical author ID.
        const byId = options.map(option => authorUsernameIdMatches(option, source))
            .filter(Boolean);
        if (byId.length) return byId.sort((a, b) => a.rank - b.rank);

        const profiles = authorProfileMetadata();
        const byLink = options.map(option => authorLinkMatches(option, source, profiles))
            .filter(Boolean);
        if (byLink.length) return byLink.sort((a, b) => a.rank - b.rank);

        const byName = options.map(option => authorNameMatches(option, source))
            .filter(Boolean);
        if (byName.length) return byName;

        // No confirmed ID, link or name match: show possible typos for review.
        return fuzzyAuthorMatches(source.username, options, profiles, source.provider);
    }

    function renderResults(query = "") {
        const results = $("author-results");
        if (!results) return;

        const trimmed = String(query || "").trim();
        const needle = trimmed.toLocaleLowerCase();
        const selected = new Set(selectedIds());
        const hosted = parseAuthorSearchLink(trimmed);

        if (!needle) {
            results.innerHTML = `<div class="author-empty">Search by name, ID, GitHub/GitLab profile or repository link.</div>`;
            return;
        }

        if (hosted?.error) {
            results.innerHTML = `<div class="author-empty">${escapeHtml(hosted.error)}</div>`;
            return;
        }

        let matches;
        if (hosted) {
            matches = findExistingAuthorsFromUrl(hosted);
        } else {
            matches = findExistingAuthorsFromText(trimmed);
        }

        const tentative = matches.length > 0 && matches.every(item => item.similar);
        const available = matches.filter(item => !selected.has(item.option.value));
        const listed = available.slice(0, 12);
        const matchedSelected = matches.length > 0 && available.length === 0;
        const found = listed.map(item => `<button type="button" class="author-result${item.similar ? " author-result-similar" : ""}" data-author-id="${escapeHtml(item.option.value)}"><strong>${escapeHtml(item.option.textContent.split(" — ")[0])}</strong><small>${escapeHtml(item.option.value)}${item.reason ? " · " + escapeHtml(item.reason) : ""}</small></button>`).join("");

        if (hosted) {
            const info = tentative
                ? "Similar authors found. Please review them before creating a new profile."
                : matches.length
                    ? (matchedSelected ? "This author is already selected." : "Existing author found. Select the correct profile:")
                    : "No existing author matched this link or account. You can create a profile.";
            const warning = typeof window.isGeneratorCatalogReady === "function" &&
                !window.isGeneratorCatalogReady()
                ? '<p class="author-link-search-note">The official author catalog has not loaded; duplicates cannot be fully checked yet.</p>'
                : "";
            const allowCreate = !matches.length || tentative;
            results.innerHTML = `<div class="author-empty">${escapeHtml(info)}</div>${found}${warning}` +
                (allowCreate ? `<div class="author-create-actions">
                    <button id="create-author" type="button" class="small-button">+ ${tentative ? "Create a different author" : "Create author from this link"}</button>
                </div>` : "");
            if (allowCreate) $("create-author").addEventListener("click", () => showCreateDialog(hosted));
        } else if (tentative) {
            results.innerHTML = `<div class="author-empty">Did you mean one of these authors? Check before creating a new profile.</div>${found}
                <div class="author-create-actions">
                    <button id="create-author" type="button" class="small-button">+ Create a different author</button>
                </div>`;
            $("create-author").addEventListener("click", () => showCreateDialog());
        } else if (available.length) {
            results.innerHTML = found;
        } else if (matches.length) {
            results.innerHTML = `<div class="author-empty">This author is already selected.</div>`;
        } else {
            results.innerHTML = `
                <div class="author-empty">No matching author found.</div>
                <div class="author-create-actions">
                    <button id="create-author" type="button" class="small-button">+ Create new author</button>
                </div>`;
            $("create-author").addEventListener("click", () => showCreateDialog());
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

    function showCreateDialog(source = null) {
        const dialog = $("new-author-dialog");
        if (!dialog) return;
        const searchName = source?.username || $("author-search").value.trim();
        const existing = source ? null : getExistingAuthorByName(searchName);
        if (existing) {
            $("new-author-error").textContent = `This author already exists: ${existing.name} (${existing.id}). Search and select that profile instead.`;
            return;
        }
        $("new-author-name").value = searchName;
        $("new-author-id").value = slugify(searchName);
        $("new-author-id").dataset.profileSource = "";
        $("new-author-avatar").value = AUTHOR_FALLBACK;
        $("new-author-bio").value = "";
        $("new-author-profile-url").value = "";
        $("new-author-lookup-status").textContent = "";
        $("new-author-lookup-status").dataset.state = "";
        $("new-author-name").dataset.previousName = searchName;
        $("new-author-links").innerHTML = "";
        if (source?.repositoryUrl) {
            addAuthorLinkRow({
                type: "Repository",
                name: "Project repository",
                url: source.repositoryUrl
            });
        } else {
            addAuthorLinkRow();
        }
        $("new-author-error").textContent = "";
        authorDialogPreviousFocus = document.activeElement;
        dialog.hidden = false;
        document.body.classList.add("author-dialog-open");
        dialog.dispatchEvent(new Event("author-dialog-open"));
        if (source?.profileUrl && typeof window.autofillAuthorFromUrl === "function") {
            window.autofillAuthorFromUrl(source.profileUrl);
        } else {
            $("new-author-profile-url").focus();
        }
    }

    function closeCreateDialog() {
        const dialog = $("new-author-dialog");
        const wasOpen = dialog && !dialog.hidden;
        if (dialog) {
            dialog.hidden = true;
            dialog.dispatchEvent(new Event("author-dialog-close"));
        }
        document.body.classList.remove("author-dialog-open");
        if (wasOpen) {
            const target = authorDialogPreviousFocus?.isConnected ? authorDialogPreviousFocus : $("author-search");
            target?.focus();
        }
        authorDialogPreviousFocus = null;
    }

    function validateAuthorForm() {
        const name = $("new-author-name").value.trim();
        const id = $("new-author-id").value.trim() || slugify(name);
        const avatar = $("new-author-avatar").value.trim();
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

        if (avatar && !isHttpUrl(avatar)) {
            error.textContent = "Avatar must use an http:// or https:// URL.";
            return null;
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
            links
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
                const data = window.PSVitaAliveCatalogSchemaV2.toV1(JSON.parse(await file.text()));
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
        const blob = new Blob([JSON.stringify(window.PSVitaAliveCatalogSchemaV2.toV2(object), null, 2) + "\n"], { type: "application/json;charset=utf-8" });
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
                <input id="author-search" class="author-search" type="search" autocomplete="off" placeholder="Search author name, ID or GitHub/GitLab URL..." aria-label="Search authors">
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
                <div class="new-author-dialog-card" role="dialog" aria-modal="true" aria-labelledby="new-author-title" aria-describedby="new-author-desc">
                    <div class="author-dialog-header">
                        <div>
                            <span class="author-dialog-eyebrow">AUTHOR PROFILE</span>
                            <h3 id="new-author-title">Create new author</h3>
                            <p id="new-author-desc">Import a public GitHub or GitLab profile, or enter the details manually.</p>
                        </div>
                        <button id="close-author-dialog" class="author-dialog-close" type="button" aria-label="Close author dialog">&times;</button>
                    </div>
                    <div class="author-dialog-body">
                        <section class="author-lookup-panel" aria-label="Import public developer profile">
                            <label for="new-author-profile-url">GitHub / GitLab profile URL</label>
                            <div class="author-lookup-controls">
                                <input id="new-author-profile-url" type="url" inputmode="url" autocomplete="off" placeholder="https://github.com/username">
                                <button id="lookup-author-profile" type="button" class="small-button">Auto-fill</button>
                            </div>
                            <p class="author-lookup-help">Paste a public user profile URL to fill the details automatically. Review all fields before saving.</p>
                            <p id="new-author-lookup-status" class="author-lookup-status" role="status" aria-live="polite"></p>
                        </section>
                        <div class="author-profile-preview">
                            <img id="new-author-avatar-preview" src="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png" alt="Author avatar preview" width="64" height="64">
                            <div class="author-preview-meta">
                                <strong id="new-author-name-preview">New author</strong>
                                <span id="new-author-id-preview">Profile details</span>
                            </div>
                            <span class="author-profile-tag">PREVIEW</span>
                        </div>
                        <div class="author-dialog-grid">
                            <label for="new-author-name">Display name <span class="author-required">*</span>
                                <input id="new-author-name" type="text" maxlength="120" required placeholder="Developer name">
                            </label>
                            <label for="new-author-id">Author ID <span class="author-required">*</span>
                                <input id="new-author-id" type="text" maxlength="80" placeholder="developer-username" required>
                            </label>
                            <label class="author-dialog-full" for="new-author-avatar">Avatar URL
                                <input id="new-author-avatar" type="url" placeholder="https://example.org/avatar.png">
                            </label>
                            <label class="author-dialog-full" for="new-author-bio">Biography
                                <textarea id="new-author-bio" rows="3" placeholder="Developer"></textarea>
                            </label>
                            <div class="author-dialog-full author-links-section">
                                <div class="author-links-heading">
                                    <div><strong>Profile links</strong><small>Public links shown on the author page</small></div>
                                    <button id="add-author-link" type="button" class="small-button">+ Add link</button>
                                </div>
                                <div id="new-author-links" class="author-links-list"></div>
                            </div>
                        </div>
                        <p id="new-author-error" class="author-create-error" role="alert"></p>
                    </div>
                    <div class="dialog-actions">
                        <button id="cancel-author" type="button" class="small-button">Cancel</button>
                        <button id="download-author" type="button" class="small-button">Download author JSON</button>
                        <button id="save-author" type="button" class="small-button primary-small-button">Add author</button>
                    </div>
                </div>
            </div>`;

        $("author-search").addEventListener("input", event => renderResults(event.target.value));
        $("import-author-json").addEventListener("change", importAuthorFiles);
        $("cancel-author").addEventListener("click", closeCreateDialog);
        $("close-author-dialog").addEventListener("click", closeCreateDialog);
        $("new-author-dialog").addEventListener("click", event => {
            if (event.target === $("new-author-dialog")) closeCreateDialog();
        });
        document.addEventListener("keydown", event => {
            const dialog = $("new-author-dialog");
            if (dialog.hidden) return;
            if (event.key === "Escape") {
                event.preventDefault();
                closeCreateDialog();
            } else if (event.key === "Tab") {
                const focusable = [...dialog.querySelectorAll(
                    "button:not([disabled]), input:not([disabled]), textarea:not([disabled]), select:not([disabled])"
                )].filter(element => element.getClientRects().length > 0);
                if (!focusable.length) return;
                const first = focusable[0];
                const last = focusable[focusable.length - 1];
                if (event.shiftKey && document.activeElement === first) {
                    event.preventDefault();
                    last.focus();
                } else if (!event.shiftKey && document.activeElement === last) {
                    event.preventDefault();
                    first.focus();
                }
            }
        });
        $("save-author").addEventListener("click", createLocalAuthor);
        $("download-author").addEventListener("click", downloadCreatedAuthorJson);
        $("add-author-link").addEventListener("click", () => addAuthorLinkRow());
        $("new-author-name").addEventListener("input", event => {
            const currentId = $("new-author-id").value;
            const previousSlug = slugify(event.target.dataset.previousName || "");
            if (!$("new-author-id").dataset.profileSource && (!currentId || currentId === previousSlug)) {
                $("new-author-id").value = slugify(event.target.value);
            }
            event.target.dataset.previousName = event.target.value;
            $("new-author-error").textContent = "";
        });
        $("new-author-id").addEventListener("input", () => {
            $("new-author-id").dataset.profileSource = "manual";
            $("new-author-error").textContent = "";
        });

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
