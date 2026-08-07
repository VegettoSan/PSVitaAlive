/**
 * PSVitaAlive Store
 *
 * Author profile page.
 *
 * Example:
 *
 * author.html?id=vegettosandev
 */


/* ========================================
   URL
======================================== */

function getAuthorIdFromUrl() {

    const params =
        new URLSearchParams(
            window.location.search
        );

    return params.get("id");
}


/* ========================================
   Author links
======================================== */

function getAuthorLinkLabel(link) {

    if (link.name) {
        return link.name;
    }

    if (link.type) {
        return link.type;
    }

    return "Open link";
}


function renderAuthorLinks(author) {

    const container =
        document.getElementById(
            "author-links"
        );

    container.innerHTML = "";


    if (
        !Array.isArray(
            author.links
        ) ||
        author.links.length === 0
    ) {

        return;
    }


    const links = [
        ...author.links
    ];


    /*
     * Recommended link first.
     */

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

            link.className =
                "author-link";


            if (
                linkData.recommended === true
            ) {

                link.classList.add(
                    "recommended"
                );
            }


            link.href =
                linkData.url;

            link.target =
                "_blank";

            link.rel =
                "noopener noreferrer";


            link.textContent =
                getAuthorLinkLabel(
                    linkData
                );


            container.appendChild(
                link
            );

        }
    );

}


/* ========================================
   Author applications
======================================== */

function getAuthorApplications(
    authorId
) {

    if (
        !Array.isArray(
            VitaHubData.catalog
        )
    ) {
        return [];
    }


    const applications =
        VitaHubData.catalog.filter(
            app => {

                /*
                 * Current VitaHub architecture
                 * supports multiple authors.
                 */

                if (
                    Array.isArray(
                        app.author_ids
                    )
                ) {

                    return app.author_ids.includes(
                        authorId
                    );

                }


                /*
                 * Compatibility with older
                 * single-author entries.
                 */

                return (
                    app.author_id === authorId
                );

            }
        );


    /*
     * Newest / recently updated first.
     */

    applications.sort(
        (a, b) => {

            const dateA =
                new Date(
                    a.version_date || 0
                ).getTime();


            const dateB =
                new Date(
                    b.version_date || 0
                ).getTime();


            const validA =
                Number.isFinite(
                    dateA
                ) && dateA > 0;


            const validB =
                Number.isFinite(
                    dateB
                ) && dateB > 0;


            if (
                !validA &&
                validB
            ) {
                return 1;
            }


            if (
                validA &&
                !validB
            ) {
                return -1;
            }


            if (
                !validA &&
                !validB
            ) {
                return 0;
            }


            return dateB - dateA;

        }
    );


    return applications;

}


/* ========================================
   Render applications
======================================== */

function renderAuthorApplications(
    applications
) {

    const container =
        document.getElementById(
            "author-apps"
        );


    container.innerHTML = "";


    document.getElementById(
        "author-app-count"
    ).textContent =
        `${applications.length} ${
            applications.length === 1
                ? "application"
                : "applications"
        }`;


    if (
        applications.length === 0
    ) {

        const empty =
            document.createElement(
                "p"
            );

        empty.className =
            "author-empty";

        empty.textContent =
            "This author has no applications in the catalog yet.";


        container.appendChild(
            empty
        );

        return;
    }


    applications.forEach(
        app => {

            const card =
                renderAppCard(
                    app
                );


            /*
             * Clicking the card opens
             * the application page.
             */

            card.classList.add(
                "app-card-clickable"
            );


            card.addEventListener(
                "click",
                event => {

                    /*
                     * Do not intercept clicks
                     * on author/category links.
                     */

                    if (
                        event.target.closest(
                            "a"
                        )
                    ) {
                        return;
                    }


                    window.location.href =
                        `app.html?title_id=${
                            encodeURIComponent(
                                app.title_id
                            )
                        }`;

                }
            );


            container.appendChild(
                card
            );

        }
    );

}


/* ========================================
   Render author
======================================== */

function renderAuthor(
    author,
    applications
) {

    document.title =
        `${author.name} — PSVitaAlive Store`;


    /* Name */

    document.getElementById(
        "author-name"
    ).textContent =
        author.name ||
        "Unknown author";


    /* Bio */

    document.getElementById(
        "author-bio"
    ).textContent =
        author.bio ||
        "No biography available.";


    /* Avatar */

    const avatar =
        document.getElementById(
            "author-avatar"
        );


    if (author.avatar) {

        avatar.src =
            resolveAssetPath(
                author.avatar
            );

    }


    avatar.alt =
        `${author.name} avatar`;


    /*
     * Fallback if the author's
     * avatar cannot be loaded.
     */

    avatar.addEventListener(
        "error",
        () => {

            avatar.src =
                resolveAssetPath(
                    "authors/icon/autoricon.png"
                );

        },
        {
            once: true
        }
    );


    /* Links */

    renderAuthorLinks(
        author
    );


    /* Applications */

    renderAuthorApplications(
        applications
    );


    /* Show */

    document.getElementById(
        "author-loading"
    ).hidden = true;


    document.getElementById(
        "author-content"
    ).hidden = false;

}


/* ========================================
   Error
======================================== */

function showAuthorError() {

    document.getElementById(
        "author-loading"
    ).hidden = true;


    document.getElementById(
        "author-error"
    ).hidden = false;

}


/* ========================================
   Initialization
======================================== */

async function initAuthorPage() {

    try {

        const authorId =
            getAuthorIdFromUrl();


        if (!authorId) {

            showAuthorError();

            return;
        }


        /*
         * Load the official generated
         * VitaHub catalogs.
         */

        await loadVitaHubData();


        /*
         * Find author by ID.
         */

        const author =
            getAuthorById(
                authorId
            );


        if (!author) {

            console.error(
                "Author not found:",
                authorId
            );

            showAuthorError();

            return;
        }


        /*
         * Find every application
         * associated with this author.
         */

        const applications =
            getAuthorApplications(
                authorId
            );


        renderAuthor(
            author,
            applications
        );


        console.log(
            "Author page loaded:",
            author.name
        );

    } catch (error) {

        console.error(
            "Failed to load author page:",
            error
        );

        showAuthorError();

    }

}


/* ========================================
   Start
======================================== */

document.addEventListener(
    "DOMContentLoaded",
    initAuthorPage
);