/**
 * PSVitaAlive Store
 *
 * Author profile page.
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
   Author data loading
======================================== */

async function loadAuthorsWithRetry(
    attempts = 4
) {

    let lastError = null;

    for (
        let attempt = 1;
        attempt <= attempts;
        attempt++
    ) {

        try {

            /*
             * Always use the official generated authors.json.
             * Cache-busting is only used after the first attempt.
             */

            const suffix =
                attempt === 1
                    ? ""
                    : `?author_retry=${Date.now()}`;


            const authors =
                await loadJSON(
                    `${VITAHUB_RAW_BASE}/authors.json${suffix}`
                );


            if (
                !Array.isArray(authors)
            ) {

                throw new Error(
                    "authors.json did not return an array"
                );

            }


            VitaHubData.authors =
                authors;


            return authors;

        } catch (error) {

            lastError = error;

            console.error(
                `Failed to load authors.json (attempt ${attempt}/${attempts}):`,
                error
            );


            if (
                attempt < attempts
            ) {

                await new Promise(
                    resolve =>
                        setTimeout(
                            resolve,
                            500 * attempt
                        )
                );

            }

        }

    }


    throw lastError ||
        new Error(
            "Unable to load authors."
        );

}


async function loadAuthorPageData() {

    /*
     * First load the normal VitaHub data.
     * catalog/categories are also required by
     * the author page cards.
     */

    await loadVitaHubData();


    /*
     * If authors were already loaded, use them.
     * Otherwise explicitly retry authors.json.
     */

    if (
        !Array.isArray(
            VitaHubData.authors
        ) ||
        VitaHubData.authors.length === 0
    ) {

        await loadAuthorsWithRetry();

    }


    return VitaHubData;

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
   Applications
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

                if (
                    Array.isArray(
                        app.author_ids
                    )
                ) {

                    return app.author_ids.includes(
                        authorId
                    );

                }


                return (
                    app.author_id === authorId
                );

            }
        );


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

            return (
                (Number.isFinite(dateB) ? dateB : 0) -
                (Number.isFinite(dateA) ? dateA : 0)
            );

        }
    );


    return applications;

}


/* ========================================
   Render
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

            card.classList.add(
                "app-card-clickable"
            );


            card.addEventListener(
                "click",
                event => {

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


function renderAuthor(
    author,
    applications
) {

    document.title =
        `${author.name} — PSVitaAlive Store`;


    document.getElementById(
        "author-name"
    ).textContent =
        author.name ||
        "Unknown author";


    document.getElementById(
        "author-bio"
    ).textContent =
        author.bio ||
        "No biography available.";


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


    renderAuthorLinks(
        author
    );


    renderAuthorApplications(
        applications
    );


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

function showAuthorError(
    message = null
) {

    document.getElementById(
        "author-loading"
    ).hidden = true;


    const error =
        document.getElementById(
            "author-error"
        );


    if (message) {

        const paragraph =
            error.querySelector(
                "p"
            );

        if (paragraph) {
            paragraph.textContent =
                message;
        }

    }


    error.hidden = false;

}


/* ========================================
   Initialize
======================================== */

async function initAuthorPage() {

    const authorId =
        getAuthorIdFromUrl();


    /*
     * No ID is a real error. Do not perform
     * catalog requests in this case.
     */

    if (!authorId) {

        showAuthorError();

        return;

    }


    try {

        /*
         * Keep "Loading author..." visible
         * for the entire request sequence.
         */

        const data =
            await loadAuthorPageData();


        let author =
            getAuthorById(
                authorId
            );


        /*
         * One extra direct refresh if the author
         * is still missing. This prevents a stale
         * authors.json response from becoming a
         * false "Author not found".
         */

        if (!author) {

            await loadAuthorsWithRetry(
                2
            );


            author =
                getAuthorById(
                    authorId
                );

        }


        if (!author) {

            console.error(
                "Author not found after loading authors.json:",
                authorId
            );


            showAuthorError();

            return;

        }


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


        /*
         * Only show the error after all
         * loading/retry attempts failed.
         */

        showAuthorError();

    }

}


document.addEventListener(
    "DOMContentLoaded",
    initAuthorPage
);
