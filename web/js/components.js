/**
 * Shared VitaHub UI components.
 */

function formatFileSize(bytes) {
    if (bytes === null || bytes === undefined || bytes === "") {
        return "Unknown size";
    }

    const value = Number(bytes);

    if (!Number.isFinite(value) || value < 0) {
        return "Unknown size";
    }

    if (value < 1024) return `${value} B`;
    if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KB`;
    if (value < 1024 * 1024 * 1024) {
        return `${(value / (1024 * 1024)).toFixed(1)} MB`;
    }

    return `${(value / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function getCategoryName(categoryId) {
    const category = getCategoryById(categoryId);
    return category ? category.name : (categoryId || "Unknown");
}

/**
 * VitaDB blocks cross-site <img> embeds (Sec-Fetch / anti-hotlink).
 * Proxy only icon files hosted on rinnegatamante.eu so the store website
 * can display them; downloads and other hosts are left unchanged.
 */
function proxyVitaDbIconUrl(url) {
    if (!url || typeof url !== "string") {
        return url;
    }

    if (url.includes("images.weserv.nl")) {
        return url;
    }

    const isVitaDbHost = /(?:^|\/\/)(?:www\.)?rinnegatamante\.eu\//i.test(url);
    const isIconPath = /\/vitadb\/icons\//i.test(url);

    if (!isVitaDbHost || !isIconPath) {
        return url;
    }

    return `https://images.weserv.nl/?url=${encodeURIComponent(url)}`;
}

function resolveAssetPath(path) {
    if (!path) return null;

    let resolved;

    if (
        path.startsWith("http://") ||
        path.startsWith("https://") ||
        path.startsWith("data:") ||
        path.startsWith("/")
    ) {
        resolved = path;
    } else if (path.startsWith("../")) {
        resolved = `${VITAHUB_RAW_BASE}/${path.substring(3)}`;
    } else {
        resolved = `${VITAHUB_RAW_BASE}/${path}`;
    }

    return proxyVitaDbIconUrl(resolved);
}

function getCategoryIconFallback(categoryId) {
    if (!categoryId) {
        return "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png";
    }

    return (
        "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/" +
        `categories/icons/${encodeURIComponent(categoryId)}.png`
    );
}

function renderAppCard(app) {
    const article = document.createElement("article");
    article.className = "app-card";

    const iconContainer = document.createElement("div");
    iconContainer.className = "app-card-icon";

    const icon = document.createElement("img");
    const fallbackUrl =
        getCategoryIconFallback(app.category_id);

    icon.src =
        resolveAssetPath(app.icon) ||
        fallbackUrl;

    icon.alt = `${app.name || "Application"} icon`;
    icon.loading = "lazy";

    let fallbackUsed = false;

    icon.addEventListener("error", () => {
        if (!fallbackUsed && icon.src !== fallbackUrl) {
            fallbackUsed = true;
            icon.src = fallbackUrl;
            return;
        }

        icon.remove();

        iconContainer.innerHTML = `
            <div class="placeholder-icon">
                APP
            </div>
        `;
    });

    iconContainer.appendChild(icon);

    const content = document.createElement("div");
    content.className = "app-card-content";

    const authors =
        getAuthorsByIds(app.author_ids);

    const top = document.createElement("div");
    top.className = "app-card-top";

    const status = document.createElement("span");
    status.className = "status-badge";
    status.textContent = app.status || "Unknown";

    top.appendChild(status);

    const title = document.createElement("h3");
    title.textContent =
        app.name || "Unknown application";

    const authorsContainer =
        document.createElement("div");

    authorsContainer.className =
        "app-card-authors";

    if (authors.length > 0) {
        authors.forEach((author, index) => {
            const authorLink =
                document.createElement("a");

            authorLink.className =
                "app-card-author";

            authorLink.href =
                `author.html?id=${encodeURIComponent(author.id)}`;

            authorLink.textContent =
                author.name;

            authorsContainer.appendChild(
                authorLink
            );

            if (index < authors.length - 1) {
                const separator =
                    document.createElement("span");

                separator.className =
                    "app-card-author-separator";

                separator.textContent =
                    ", ";

                authorsContainer.appendChild(
                    separator
                );
            }
        });
    } else {
        const unknownAuthor =
            document.createElement("span");

        unknownAuthor.className =
            "app-card-author";

        unknownAuthor.textContent =
            "Unknown author";

        authorsContainer.appendChild(
            unknownAuthor
        );
    }

    const description =
        document.createElement("p");

    description.className =
        "app-card-description";

    description.textContent =
        app.description ||
        "No description available.";

    const meta =
        document.createElement("div");

    meta.className =
        "app-card-meta";

    const version =
        document.createElement("span");

    version.textContent =
        app.version
            ? `v${app.version}`
            : "Unknown version";

    const size =
        document.createElement("span");

    size.textContent =
        formatFileSize(app.size);

    const category =
        document.createElement("a");

    category.className =
        "app-card-category";

    category.href =
        `category.html?id=${encodeURIComponent(app.category_id)}`;

    category.textContent =
        getCategoryName(app.category_id);

    meta.appendChild(version);
    meta.appendChild(size);
    meta.appendChild(category);

    content.appendChild(top);
    content.appendChild(title);
    content.appendChild(authorsContainer);
    content.appendChild(description);
    content.appendChild(meta);

    article.appendChild(iconContainer);
    article.appendChild(content);

    article.classList.add("app-card-clickable");

    article.addEventListener("click", event => {
        if (event.target.closest("a")) return;
        if (!app.title_id) return;

        window.location.href =
            `app.html?title_id=${encodeURIComponent(app.title_id)}`;
    });

    return article;
}

function renderOfficialGameCard(game) {
    const article = document.createElement("article");
    article.className =
        "app-card official-game-card app-card-clickable";

    const coverContainer =
        document.createElement("div");

    coverContainer.className =
        "app-card-icon official-game-cover";

    if (game.cover) {
        const cover =
            document.createElement("img");

        cover.src = game.cover;
        cover.alt = `${game.name} cover`;
        cover.loading = "lazy";

        cover.addEventListener("error", () => {
            cover.remove();
            coverContainer.innerHTML = `
                <div class="placeholder-icon">
                    GAME
                </div>
            `;
        });

        coverContainer.appendChild(cover);
    } else {
        coverContainer.innerHTML = `
            <div class="placeholder-icon">
                GAME
            </div>
        `;
    }

    const content =
        document.createElement("div");

    content.className =
        "app-card-content";

    const top =
        document.createElement("div");

    top.className =
        "app-card-top";

    const badge =
        document.createElement("span");

    badge.className =
        "status-badge official-game-badge";

    badge.textContent =
        "PS Vita Game";

    top.appendChild(badge);

    const title =
        document.createElement("h3");

    title.textContent =
        game.name || "Unknown game";

    const description =
        document.createElement("p");

    description.className =
        "app-card-description";

    description.textContent =
        game.description ||
        "No description available.";

    const meta =
        document.createElement("div");

    meta.className =
        "app-card-meta";

    const version =
        document.createElement("span");

    version.textContent =
        game.version
            ? `v${game.version}`
            : "Unknown version";

    const size =
        document.createElement("span");

    size.textContent =
        formatFileSize(game.size);

    const category =
        document.createElement("span");

    category.textContent =
        getCategoryName(game.category_id);

    meta.appendChild(version);
    meta.appendChild(size);
    meta.appendChild(category);

    content.appendChild(top);
    content.appendChild(title);
    content.appendChild(description);
    content.appendChild(meta);

    article.appendChild(coverContainer);
    article.appendChild(content);

    article.addEventListener("click", () => {
        window.location.href =
            `game.html?id=${encodeURIComponent(game.id)}`;
    });

    return article;
}
