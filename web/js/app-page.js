/**
 * PSVitaAlive Store
 *
 * Application detail page.
 *
 * Example:
 *
 * app.html?title_id=HALOCE00101
 */


/* ========================================
   URL
======================================== */

function getTitleIdFromUrl() {

    const params =
        new URLSearchParams(
            window.location.search
        );

    return params.get("title_id");
}


/* ========================================
   Helpers
======================================== */

function displayValue(
    value,
    fallback = "Not available"
) {

    if (
        value === null ||
        value === undefined ||
        value === ""
    ) {
        return fallback;
    }

    return String(value);
}


function formatReleaseDate(date) {

    if (!date) {
        return "Unknown";
    }

    const parsed =
        new Date(date);

    if (
        Number.isNaN(
            parsed.getTime()
        )
    ) {
        return date;
    }

    return parsed.toLocaleDateString(
        undefined,
        {
            year: "numeric",
            month: "long",
            day: "numeric"
        }
    );
}


/* ========================================
   Authors
======================================== */

function renderAuthors(authors) {

    const container =
        document.getElementById(
            "app-authors"
        );

    container.innerHTML = "";


    if (
        !Array.isArray(authors) ||
        authors.length === 0
    ) {

        container.textContent =
            "Unknown author";

        return;
    }


    const label =
        document.createElement(
            "span"
        );

    label.className =
        "app-meta-label";

    label.textContent =
        "By";

    container.appendChild(
        label
    );


    authors.forEach(
        (author, index) => {

            const link =
                document.createElement(
                    "a"
                );

            link.href =
                `author.html?id=${
                    encodeURIComponent(
                        author.id
                    )
                }`;

            link.textContent =
                author.name;

            link.className =
                "app-author-link";


            container.appendChild(
                link
            );


            if (
                index <
                authors.length - 1
            ) {

                const separator =
                    document.createElement(
                        "span"
                    );

                separator.textContent =
                    ", ";

                container.appendChild(
                    separator
                );
            }

        }
    );

}


/* ========================================
   Category
======================================== */

function renderCategory(
    app,
    category
) {

    const container =
        document.getElementById(
            "app-category"
        );

    container.innerHTML = "";


    if (!category) {

        container.textContent =
            "Unknown category";

        return;
    }


    const categoryLink =
        document.createElement(
            "a"
        );

    categoryLink.href =
        `category.html?id=${
            encodeURIComponent(
                category.id
            )
        }`;

    categoryLink.textContent =
        category.name;

    categoryLink.className =
        "app-category-link";


    container.appendChild(
        categoryLink
    );


    const subcategories =
        Array.isArray(
            category.subcategories
        )
            ? category.subcategories
            : [];


    const selected =
        Array.isArray(
            app.subcategory_ids
        )
            ? app.subcategory_ids
            : [];


    const names =
        selected
            .map(id =>
                subcategories.find(
                    sub =>
                        sub.id === id
                )
            )
            .filter(Boolean)
            .map(
                sub => sub.name
            );


    if (names.length > 0) {

        const separator =
            document.createElement(
                "span"
            );

        separator.textContent =
            " · ";

        container.appendChild(
            separator
        );


        const subcategoryText =
            document.createElement(
                "span"
            );

        subcategoryText.className =
            "app-subcategories";

        subcategoryText.textContent =
            names.join(" · ");


        container.appendChild(
            subcategoryText
        );
    }

}


/* ========================================
   Screenshots
======================================== */

function renderScreenshots(app) {

    const section =
        document.getElementById(
            "screenshots-section"
        );

    const container =
        document.getElementById(
            "app-screenshots"
        );

    container.innerHTML = "";


    if (
        !Array.isArray(
            app.screenshots
        ) ||
        app.screenshots.length === 0
    ) {

        section.hidden = true;

        return;
    }


    section.hidden = false;


    app.screenshots.forEach(
        (screenshot, index) => {

            const url =
                resolveAssetPath(
                    screenshot
                );


            const link =
                document.createElement(
                    "a"
                );

            link.href =
                url;

            link.target =
                "_blank";

            link.rel =
                "noopener noreferrer";

            link.className =
                "app-screenshot";


            const image =
                document.createElement(
                    "img"
                );

            image.src =
                url;

            image.alt =
                `${app.name} screenshot ${
                    index + 1
                }`;

            image.loading =
                "lazy";


            link.appendChild(
                image
            );

            container.appendChild(
                link
            );

        }
    );

}


/* ========================================
   Links
======================================== */

function getLinkLabel(link) {

    if (link.label) {
        return link.label;
    }

    if (link.type) {
        return link.type;
    }

    return "Open link";
}


function renderLinks(app) {

    const section =
        document.getElementById(
            "links-section"
        );

    const container =
        document.getElementById(
            "app-links"
        );

    container.innerHTML = "";


    if (
        !Array.isArray(
            app.links
        ) ||
        app.links.length === 0
    ) {

        section.hidden = true;

        return;
    }


    section.hidden = false;


    /*
     * Recommended link first.
     */

    const links = [
        ...app.links
    ];


    links.sort(
        (a, b) => {

            if (
                a.recommended === true &&
                b.recommended !== true
            ) {
                return -1;
            }

            if (
                a.recommended !== true &&
                b.recommended === true
            ) {
                return 1;
            }

            return 0;
        }
    );


    links.forEach(
        linkData => {

            if (!linkData.url) {
                return;
            }


            const link =
                document.createElement(
                    "a"
                );

            link.href =
                linkData.url;

            link.target =
                "_blank";

            link.rel =
                "noopener noreferrer";

            link.className =
                "app-link-button";


            if (
                linkData.recommended === true
            ) {

                link.classList.add(
                    "recommended"
                );
            }


            link.textContent =
                getLinkLabel(
                    linkData
                );


            container.appendChild(
                link
            );

        }
    );

}


/* ========================================
   Render application
======================================== */

function renderApplication(
    app,
    authors,
    category
) {

    document.title =
        `${app.name} — PSVitaAlive Store`;


    /* Name */

    document.getElementById(
        "app-name"
    ).textContent =
        displayValue(
            app.name,
            "Unknown application"
        );


    /* Status */

    document.getElementById(
        "app-status"
    ).textContent =
        displayValue(
            app.status,
            "Unknown"
        );


    /* Version */

    document.getElementById(
        "app-version"
    ).textContent =
        app.version
            ? `Version ${app.version}`
            : "Version unknown";


    /* Icon */

    const icon =
        document.getElementById(
            "app-icon"
        );


    if (app.icon) {

        icon.src =
            resolveAssetPath(
                app.icon
            );

        icon.alt =
            `${app.name} icon`;

    }


    /* Authors */

    renderAuthors(
        authors
    );


    /* Category */

    renderCategory(
        app,
        category
    );


    /* Short description */

    document.getElementById(
        "app-description"
    ).textContent =
        displayValue(
            app.description
        );


    /* Long description */

    document.getElementById(
        "app-long-description"
    ).textContent =
        displayValue(
            app.long_description
        );


    /* Version */

    document.getElementById(
        "info-version"
    ).textContent =
        displayValue(
            app.version
        );


    /* Date */

    document.getElementById(
        "info-date"
    ).textContent =
        formatReleaseDate(
            app.version_date
        );


    /* Size */

    document.getElementById(
        "info-size"
    ).textContent =
        formatFileSize(
            app.size
        );


    /* Requirements */

    document.getElementById(
        "info-requirements"
    ).textContent =
        displayValue(
            app.requirements
        );


    /* Title ID */

    document.getElementById(
        "info-title-id"
    ).textContent =
        displayValue(
            app.title_id
        );


    /* Screenshots */

    renderScreenshots(
        app
    );


    /* Links */

    renderLinks(
        app
    );


    /* Show page */

    document.getElementById(
        "app-loading"
    ).hidden = true;


    document.getElementById(
        "app-content"
    ).hidden = false;

}


/* ========================================
   Error
======================================== */

function showApplicationError() {

    document.getElementById(
        "app-loading"
    ).hidden = true;


    document.getElementById(
        "app-error"
    ).hidden = false;

}


/* ========================================
   Initialization
======================================== */

async function initApplicationPage() {

    try {

        const titleId =
            getTitleIdFromUrl();


        if (!titleId) {

            showApplicationError();

            return;
        }


        /*
         * Load the official generated
         * VitaHub catalogs.
         */

        await loadVitaHubData();


        /*
         * Find application using
         * Title ID, not internal ID.
         */

        const app =
            getAppByTitleId(
                titleId
            );


        if (!app) {

            console.error(
                "Application not found:",
                titleId
            );

            showApplicationError();

            return;
        }


        /*
         * Resolve all authors.
         */

        const authors =
            getAuthorsByIds(
                app.author_ids
            );


        /*
         * Resolve category.
         */

        const category =
            getCategoryById(
                app.category_id
            );


        /*
         * Render everything.
         */

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


/* ========================================
   Start
======================================== */

document.addEventListener(
    "DOMContentLoaded",
    initApplicationPage
);