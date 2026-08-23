/**
 * PSVitaAlive Store
 * Application detail page.
 */

function getTitleIdFromUrl() {
    const params = new URLSearchParams(window.location.search);
    return params.get("title_id");
}

function displayValue(value, fallback = "Not available") {
    if (value === null || value === undefined || value === "") {
        return fallback;
    }
    return String(value);
}

function formatReleaseDate(date) {
    if (!date) return "Unknown";

    const parsed = new Date(date);
    if (Number.isNaN(parsed.getTime())) return date;

    return parsed.toLocaleDateString(undefined, {
        year: "numeric",
        month: "long",
        day: "numeric"
    });
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

function renderAuthors(authors) {
    const container =
        document.getElementById("app-authors");

    container.innerHTML = "";

    if (!Array.isArray(authors) || authors.length === 0) {
        container.textContent = "Unknown author";
        return;
    }

    const label =
        document.createElement("span");

    label.className =
        "app-meta-label";

    label.textContent =
        "By";

    container.appendChild(label);

    authors.forEach((author, index) => {
        const link =
            document.createElement("a");

        link.href =
            `author.html?id=${encodeURIComponent(author.id)}`;

        link.textContent =
            author.name;

        link.className =
            "app-author-link";

        container.appendChild(link);

        if (index < authors.length - 1) {
            const separator =
                document.createElement("span");

            separator.textContent =
                ", ";

            container.appendChild(separator);
        }
    });
}

function renderCategory(app, category) {
    const container =
        document.getElementById("app-category");

    container.innerHTML = "";

    if (!category) {
        container.textContent =
            "Unknown category";
        return;
    }

    const categoryLink =
        document.createElement("a");

    categoryLink.href =
        `category.html?id=${encodeURIComponent(category.id)}`;

    categoryLink.textContent =
        category.name;

    categoryLink.className =
        "app-category-link";

    container.appendChild(categoryLink);

    const subcategories =
        Array.isArray(category.subcategories)
            ? category.subcategories
            : [];

    const selected =
        Array.isArray(app.subcategory_ids)
            ? app.subcategory_ids
            : [];

    const names =
        selected
            .map(id =>
                subcategories.find(
                    sub => sub.id === id
                )
            )
            .filter(Boolean)
            .map(sub => sub.name);

    if (names.length > 0) {
        const separator =
            document.createElement("span");

        separator.textContent =
            " · ";

        container.appendChild(separator);

        const subcategoryText =
            document.createElement("span");

        subcategoryText.className =
            "app-subcategories";

        subcategoryText.textContent =
            names.join(" · ");

        container.appendChild(subcategoryText);
    }
}

function renderScreenshots(app) {
    const section =
        document.getElementById("screenshots-section");

    const container =
        document.getElementById("app-screenshots");

    container.innerHTML = "";

    if (
        !Array.isArray(app.screenshots) ||
        app.screenshots.length === 0
    ) {
        section.hidden = true;
        return;
    }

    section.hidden = false;

    app.screenshots.forEach((screenshot, index) => {
        const url =
            resolveAssetPath(screenshot);

        const link =
            document.createElement("a");

        link.href = url;
        link.target = "_blank";
        link.rel = "noopener noreferrer";
        link.className = "app-screenshot";

        const image =
            document.createElement("img");

        image.src = url;
        image.alt =
            `${app.name} screenshot ${index + 1}`;
        image.loading = "lazy";

        image.addEventListener("error", () => {
            link.remove();
        });

        link.appendChild(image);
        container.appendChild(link);
    });
}

function getLinkLabel(link) {
    if (link.label) return link.label;
    if (link.type) return link.type;
    return "Open link";
}

function renderLinks(app) {
    const section =
        document.getElementById("links-section");

    const container =
        document.getElementById("app-links");

    container.innerHTML = "";

    if (
        !Array.isArray(app.links) ||
        app.links.length === 0
    ) {
        section.hidden = true;
        return;
    }

    section.hidden = false;

    const links = [...app.links];

    links.sort((a, b) => {
        if (
            a.recommended === true &&
            b.recommended !== true
        ) return -1;

        if (
            a.recommended !== true &&
            b.recommended === true
        ) return 1;

        return 0;
    });

    links.forEach(linkData => {
        if (!linkData.url) return;

        const link =
            document.createElement("a");

        link.href =
            resolveDownloadUrl(linkData.url);

        link.target =
            "_blank";

        link.rel =
            "noopener noreferrer";

        link.className =
            "app-link-button";

        if (linkData.recommended === true) {
            link.classList.add("recommended");
        }

        link.textContent =
            getLinkLabel(linkData);

        container.appendChild(link);
    });
}

function renderApplication(app, authors, category) {
    document.title =
        `${app.name} — PSVitaAlive Store`;

    document.getElementById("app-name").textContent =
        displayValue(
            app.name,
            "Unknown application"
        );

    document.getElementById("app-status").textContent =
        displayValue(
            app.status,
            "Unknown"
        );

    document.getElementById("app-version").textContent =
        app.version
            ? `Version ${app.version}`
            : "Version unknown";

    const icon =
        document.getElementById("app-icon");

    const categoryFallback =
        getCategoryIconFallback(
            app.category_id
        );

    icon.alt =
        `${app.name || "Application"} icon`;

    icon.src =
        resolveAssetPath(app.icon) ||
        categoryFallback;

    let fallbackUsed = false;

    icon.addEventListener("error", () => {
        if (
            !fallbackUsed &&
            icon.src !== categoryFallback
        ) {
            fallbackUsed = true;
            icon.src = categoryFallback;
            return;
        }

        icon.removeAttribute("src");
    });

    renderAuthors(authors);
    renderCategory(app, category);

    document.getElementById("app-description").textContent =
        displayValue(app.description);

    document.getElementById("app-long-description").textContent =
        displayValue(app.long_description);

    document.getElementById("info-version").textContent =
        displayValue(app.version);

    document.getElementById("info-date").textContent =
        formatReleaseDate(app.version_date);

    document.getElementById("info-size").textContent =
        formatFileSize(app.size);

    document.getElementById("info-requirements").textContent =
        displayValue(app.requirements);

    document.getElementById("info-title-id").textContent =
        displayValue(app.title_id);

    renderScreenshots(app);
    renderLinks(app);

    document.getElementById("app-loading").hidden = true;
    document.getElementById("app-content").hidden = false;
}

function showApplicationError() {
    document.getElementById("app-loading").hidden = true;
    document.getElementById("app-error").hidden = false;
}

async function initApplicationPage() {
    try {
        const titleId =
            getTitleIdFromUrl();

        if (!titleId) {
            showApplicationError();
            return;
        }

        await loadVitaHubData();

        const app =
            getAppByTitleId(titleId);

        if (!app) {
            console.error(
                "Application not found:",
                titleId
            );

            showApplicationError();
            return;
        }

        const authors =
            getAuthorsByIds(
                app.author_ids
            );

        const category =
            getCategoryById(
                app.category_id
            );

        renderApplication(
            app,
            authors,
            category
        );

        console.log(
            "Application page loaded:",
            app.name
        );
    } catch (error) {
        console.error(
            "Failed to load application page:",
            error
        );

        showApplicationError();
    }
}

document.addEventListener(
    "DOMContentLoaded",
    initApplicationPage
);
