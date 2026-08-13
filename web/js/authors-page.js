/* ========================================
   Authors Page
======================================== */

const AUTHOR_FALLBACK_AVATAR = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png";

function getAppsByAuthor(authorId) {
    return VitaHubData.catalog.filter(app => {
        if (Array.isArray(app.author_ids)) return app.author_ids.includes(authorId);
        if (app.author_id) return app.author_id === authorId;
        return false;
    });
}

function resolveAuthorAvatar(author) {
    const avatar = author.icon || author.avatar || AUTHOR_FALLBACK_AVATAR;
    if (/^https?:\/\/github\.com\/[^/]+\.png(?:\?.*)?$/i.test(avatar)) {
        const username = avatar.replace(/^https?:\/\/github\.com\//i, "").replace(/\.png(?:\?.*)?$/i, "");
        return `https://avatars.githubusercontent.com/${encodeURIComponent(username)}?size=256`;
    }
    if (/^https?:\/\//i.test(avatar)) return avatar;
    return resolveAssetPath(avatar);
}

function renderAuthorCard(author) {
    const card = document.createElement("a");
    card.className = "author-card";
    card.href = `author.html?id=${encodeURIComponent(author.id)}`;

    const avatarContainer = document.createElement("div");
    avatarContainer.className = "author-card-avatar";

    const avatar = document.createElement("img");
    avatar.src = resolveAuthorAvatar(author);
    avatar.alt = `${author.name} avatar`;
    avatar.loading = "lazy";
    avatar.decoding = "async";
    avatar.addEventListener("error", () => {
        if (avatar.src !== AUTHOR_FALLBACK_AVATAR) {
            avatar.src = AUTHOR_FALLBACK_AVATAR;
            return;
        }
        avatar.remove();
        avatarContainer.innerHTML = `<div class="author-card-placeholder">?</div>`;
    });
    avatarContainer.appendChild(avatar);

    const content = document.createElement("div");
    content.className = "author-card-content";

    const name = document.createElement("h2");
    name.textContent = author.name || "Unknown author";

    const apps = getAppsByAuthor(author.id);
    const count = document.createElement("span");
    count.className = "author-card-count";
    count.textContent = `${apps.length} Homebrew`;

    const bio = document.createElement("p");
    bio.className = "author-card-bio";
    bio.textContent = author.bio || "PlayStation Vita Homebrew developer.";

    content.appendChild(name);
    content.appendChild(bio);
    content.appendChild(count);

    const arrow = document.createElement("span");
    arrow.className = "author-card-arrow";
    arrow.textContent = "→";

    card.appendChild(avatarContainer);
    card.appendChild(content);
    card.appendChild(arrow);
    return card;
}

function renderAuthors(authors) {
    const grid = document.getElementById("authors-grid");
    const empty = document.getElementById("authors-empty");
    const countElement = document.getElementById("authors-count");
    if (!grid) return;

    grid.innerHTML = "";
    const sortedAuthors = [...authors].sort((a, b) =>
        (a.name || "").localeCompare((b.name || ""), undefined, { sensitivity: "base" })
    );

    if (countElement) {
        countElement.textContent = `${sortedAuthors.length} ${sortedAuthors.length === 1 ? "author" : "authors"}`;
    }

    if (sortedAuthors.length === 0) {
        if (empty) empty.hidden = false;
        return;
    }
    if (empty) empty.hidden = true;

    sortedAuthors.forEach(author => grid.appendChild(renderAuthorCard(author)));
}

function filterAuthors() {
    const searchInput = document.getElementById("author-search");
    if (!searchInput) return;
    const query = searchInput.value.trim().toLowerCase();
    const filtered = VitaHubData.authors.filter(author => {
        const name = (author.name || "").toLowerCase();
        const bio = (author.bio || "").toLowerCase();
        return name.includes(query) || bio.includes(query);
    });
    renderAuthors(filtered);
}

async function initAuthorsPage() {
    try {
        await loadVitaHubData();
        renderAuthors(VitaHubData.authors);
        const searchInput = document.getElementById("author-search");
        if (searchInput) searchInput.addEventListener("input", filterAuthors);
    } catch (error) {
        console.error("Failed to initialize authors page:", error);
    }
}

document.addEventListener("DOMContentLoaded", initAuthorsPage);
