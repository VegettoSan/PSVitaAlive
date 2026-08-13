/**
 * PSVitaAlive Store
 * Author profile page.
 */

function getAuthorIdFromUrl() {
    const params = new URLSearchParams(window.location.search);
    return params.get("id");
}

const AUTHOR_FALLBACK_AVATAR =
    "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png";

function resolveAuthorProfileAvatar(author) {
    const avatar = author.avatar || author.icon || AUTHOR_FALLBACK_AVATAR;

    if (/^https?:\/\/github\.com\/[^/]+\.png(?:\?.*)?$/i.test(avatar)) {
        const username = avatar
            .replace(/^https?:\/\/github\.com\//i, "")
            .replace(/\.png(?:\?.*)?$/i, "");

        return `https://avatars.githubusercontent.com/${encodeURIComponent(username)}?size=512`;
    }

    if (/^https?:\/\//i.test(avatar)) {
        return avatar;
    }

    return resolveAssetPath(avatar);
}

function getAuthorLinkLabel(link) {
    if (link.name) return link.name;
    if (link.type) return link.type;
    return "Open link";
}

function renderAuthorLinks(author) {
    const container = document.getElementById("author-links");
    if (!container) return;

    container.innerHTML = "";

    if (!Array.isArray(author.links) || author.links.length === 0) {
        return;
    }

    const links = [...author.links].sort((a, b) => {
        if (a.recommended === true && b.recommended !== true) return -1;
        if (a.recommended !== true && b.recommended === true) return 1;
        return 0;
    });

    links.forEach(linkData => {
        if (!linkData.url) return;

        const link = document.createElement("a");
        link.className = "author-link";

        if (linkData.recommended === true) {
            link.classList.add("recommended");
        }

        link.href = linkData.url;
        link.target = "_blank";
        link.rel = "noopener noreferrer";
        link.textContent = getAuthorLinkLabel(linkData);

        container.appendChild(link);
    });
}

function getAuthorApplications(authorId) {
    if (!Array.isArray(VitaHubData.catalog)) {
        return [];
    }

    const applications = VitaHubData.catalog.filter(app => {
        if (Array.isArray(app.author_ids)) {
            return app.author_ids.includes(authorId);
        }

        return app.author_id === authorId;
    });

    applications.sort((a, b) => {
        const dateA = new Date(a.version_date || 0).getTime();
        const dateB = new Date(b.version_date || 0).getTime();

        const validA = Number.isFinite(dateA) && dateA > 0;
        const validB = Number.isFinite(dateB) && dateB > 0;

        if (!validA && validB) return 1;
        if (validA && !validB) return -1;
        if (!validA && !validB) return 0;

        return dateB - dateA;
    });

    return applications;
}

function renderAuthorApplications(applications) {
    const container = document.getElementById("author-apps");
    if (!container) return;

    container.innerHTML = "";

    const countElement = document.getElementById("author-app-count");

    if (countElement) {
        countElement.textContent =
            `${applications.length} ${
                applications.length === 1
                    ? "application"
                    : "applications"
            }`;
    }

    if (applications.length === 0) {
        const empty = document.createElement("p");
        empty.className = "author-empty";
        empty.textContent =
            "This author has no applications in the catalog yet.";

        container.appendChild(empty);
        return;
    }

    applications.forEach(app => {
        container.appendChild(renderAppCard(app));
    });
}

function renderAuthor(author, applications) {
    document.title =
        `${author.name} — PSVitaAlive Store`;

    const name =
        document.getElementById("author-name");

    const bio =
        document.getElementById("author-bio");

    const avatar =
        document.getElementById("author-avatar");

    if (name) {
        name.textContent =
            author.name || "Unknown author";
    }

    if (bio) {
        bio.textContent =
            author.bio || "No biography available.";
    }

    if (avatar) {
        let fallbackUsed = false;

        const setAvatar = value => {
            avatar.src =
                value
                    ? resolveAuthorProfileAvatar({ avatar: value })
                    : AUTHOR_FALLBACK_AVATAR;
        };

        setAvatar(
            author.avatar ||
            author.icon ||
            AUTHOR_FALLBACK_AVATAR
        );

        avatar.alt =
            `${author.name || "Unknown author"} avatar`;

        avatar.addEventListener("error", () => {
            if (!fallbackUsed) {
                fallbackUsed = true;
                avatar.src = AUTHOR_FALLBACK_AVATAR;
                return;
            }

            avatar.removeAttribute("src");
        });
    }

    renderAuthorLinks(author);
    renderAuthorApplications(applications);

    const loading =
        document.getElementById("author-loading");

    const content =
        document.getElementById("author-content");

    if (loading) loading.hidden = true;
    if (content) content.hidden = false;
}

function showAuthorError() {
    const loading =
        document.getElementById("author-loading");

    const error =
        document.getElementById("author-error");

    if (loading) loading.hidden = true;
    if (error) error.hidden = false;
}

async function findAuthorWithRetry(authorId) {
    for (let attempt = 1; attempt <= 3; attempt++) {
        const author = getAuthorById(authorId);

        if (author) {
            return author;
        }

        if (attempt < 3) {
            await new Promise(resolve =>
                setTimeout(resolve, 400 * attempt)
            );
        }
    }

    try {
        const refreshed =
            await fetch(
                `${VITAHUB_RAW_BASE}/authors.json?refresh=${Date.now()}`,
                { cache: "no-store" }
            );

        if (refreshed.ok) {
            const authors =
                await refreshed.json();

            if (Array.isArray(authors)) {
                VitaHubData.authors = authors;
                return getAuthorById(authorId);
            }
        }
    } catch (error) {
        console.warn(
            "Unable to refresh authors.json:",
            error
        );
    }

    return null;
}

async function initAuthorPage() {
    try {
        const authorId =
            getAuthorIdFromUrl();

        if (!authorId) {
            showAuthorError();
            return;
        }

        await loadVitaHubData();

        const author =
            await findAuthorWithRetry(authorId);

        if (!author) {
            console.error(
                "Author not found:",
                authorId
            );

            showAuthorError();
            return;
        }

        renderAuthor(
            author,
            getAuthorApplications(authorId)
        );
    } catch (error) {
        console.error(
            "Failed to load author page:",
            error
        );

        showAuthorError();
    }
}

document.addEventListener(
    "DOMContentLoaded",
    initAuthorPage
);
