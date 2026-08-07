/**
 * Convierte bytes a una unidad legible.
 */
function formatFileSize(bytes) {
    if (bytes === null || bytes === undefined || bytes === "") {
        return "Unknown size";
    }

    const value = Number(bytes);

    if (!Number.isFinite(value) || value < 0) {
        return "Unknown size";
    }

    if (value < 1024) {
        return `${value} B`;
    }

    if (value < 1024 * 1024) {
        return `${(value / 1024).toFixed(1)} KB`;
    }

    if (value < 1024 * 1024 * 1024) {
        return `${(value / (1024 * 1024)).toFixed(1)} MB`;
    }

    return `${(value / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}


/**
 * Obtiene el nombre de una categoría.
 */
function getCategoryName(categoryId) {
    const category = getCategoryById(categoryId);

    if (!category) {
        return categoryId || "Unknown";
    }

    return category.name;
}

/**
 * Resuelve una ruta de recurso del catálogo
 * desde las páginas de VitaHub.
 */
function resolveAssetPath(path) {
    if (!path) {
        return null;
    }

    // URLs externas
    if (
        path.startsWith("http://") ||
        path.startsWith("https://") ||
        path.startsWith("data:") ||
        path.startsWith("/")
    ) {
        return path;
    }

    // Evitar añadir ../ dos veces
    if (path.startsWith("../")) {
        return `${VITAHUB_RAW_BASE}/${path.substring(3)}`;
    }

    return `${VITAHUB_RAW_BASE}/${path}`;
}

/**
 * Crea una tarjeta de aplicación.
 */
function renderAppCard(app) {
    const article = document.createElement("article");

    article.className = "app-card";

    const iconContainer = document.createElement("div");
    iconContainer.className = "app-card-icon";

    if (app.icon) {
        const icon = document.createElement("img");

        icon.src = resolveAssetPath(app.icon);
        icon.alt = `${app.name} icon`;
        icon.loading = "lazy";

        icon.addEventListener("error", () => {
            icon.remove();
            iconContainer.innerHTML = `
                <div class="placeholder-icon">
                    APP
                </div>
            `;
        });

        iconContainer.appendChild(icon);
    } else {
        iconContainer.innerHTML = `
            <div class="placeholder-icon">
                APP
            </div>
        `;
    }


    const content = document.createElement("div");
    content.className = "app-card-content";

    const authors = getAuthorsByIds(app.author_ids);


    const top = document.createElement("div");
    top.className = "app-card-top";

    const status = document.createElement("span");
    status.className = "status-badge";
    status.textContent = app.status || "Unknown";

    top.appendChild(status);


    const title = document.createElement("h3");
    title.textContent = app.name || "Unknown application";

    const authorsContainer = document.createElement("div");

authorsContainer.className = "app-card-authors";

if (authors.length > 0) {
    authors.forEach((author, index) => {
        const authorLink = document.createElement("a");

        authorLink.className = "app-card-author";
        authorLink.href =
            `author.html?id=${encodeURIComponent(author.id)}`;

        authorLink.textContent = author.name;

        authorsContainer.appendChild(authorLink);

        if (index < authors.length - 1) {
            const separator = document.createElement("span");

            separator.className = "app-card-author-separator";
            separator.textContent = ", ";

            authorsContainer.appendChild(separator);
        }
    });
} else {
    const unknownAuthor = document.createElement("span");

    unknownAuthor.className = "app-card-author";
    unknownAuthor.textContent = "Unknown author";

    authorsContainer.appendChild(unknownAuthor);
}

    const description = document.createElement("p");
    description.className = "app-card-description";
    description.textContent =
        app.description || "No description available.";


    const meta = document.createElement("div");
    meta.className = "app-card-meta";


    const version = document.createElement("span");
    version.textContent =
        app.version ? `v${app.version}` : "Unknown version";


    const size = document.createElement("span");
    size.textContent = formatFileSize(app.size);


    const category = document.createElement("a");

    category.className = "app-card-category";
    category.href =
        `category.html?id=${encodeURIComponent(app.category_id)}`;

    category.textContent = getCategoryName(app.category_id);


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


    /*
     * Open application detail page
     * when clicking the card.
     *
     * The application is identified
     * by its Title ID.
     */
    article.classList.add("app-card-clickable");

    article.addEventListener("click", event => {

        /*
         * Do not intercept clicks on
         * author/category links.
         */
        if (event.target.closest("a")) {
            return;
        }

        if (!app.title_id) {
            return;
        }

        window.location.href =
            `app.html?title_id=${encodeURIComponent(
                app.title_id
            )}`;
    });




    return article;
}

/**
 * Crea una tarjeta para un juego oficial de PS Vita.
 */
function renderOfficialGameCard(game) {

    const article =
        document.createElement("article");

    article.className =
        "app-card official-game-card app-card-clickable";


    /* ========================================
       Cover
    ======================================== */

    const coverContainer =
        document.createElement("div");

    coverContainer.className =
        "app-card-icon official-game-cover";


    if (game.cover) {

        const cover =
            document.createElement("img");

        cover.src =
            game.cover;

        cover.alt =
            `${game.name} cover`;

        cover.loading =
            "lazy";

        coverContainer.appendChild(
            cover
        );

    } else {

        coverContainer.innerHTML = `
            <div class="placeholder-icon">
                GAME
            </div>
        `;

    }


    /* ========================================
       Content
    ======================================== */

    const content =
        document.createElement("div");

    content.className =
        "app-card-content";


    /* Type */

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


    top.appendChild(
        badge
    );


    /* Title */

    const title =
        document.createElement("h3");

    title.textContent =
        game.name ||
        "Unknown game";


    /* Description */

    const description =
        document.createElement("p");

    description.className =
        "app-card-description";

    description.textContent =
        game.description ||
        "No description available.";


    /* Meta */

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
        getCategoryName(
            game.category_id
        );


    meta.appendChild(
        version
    );

    meta.appendChild(
        size
    );

    meta.appendChild(
        category
    );


    /* Assemble */

    content.appendChild(
        top
    );

    content.appendChild(
        title
    );

    content.appendChild(
        description
    );

    content.appendChild(
        meta
    );


    article.appendChild(
        coverContainer
    );

    article.appendChild(
        content
    );


    /*
     * Detail page.
     *
     * The page itself will be created
     * in the next phase.
     */

    article.addEventListener(
        "click",
        () => {

            window.location.href =
                `game.html?id=${encodeURIComponent(
                    game.id
                )}`;

        }
    );


    return article;
}