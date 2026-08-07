/* ========================================
   Authors Page
======================================== */


/**
 * Obtiene todos los Homebrew
 * asociados a un autor.
 */
function getAppsByAuthor(authorId) {

    return VitaHubData.catalog.filter(
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


            if (app.author_id) {

                return app.author_id === authorId;

            }


            return false;

        }
    );

}


/**
 * Crea una tarjeta de autor.
 */
function renderAuthorCard(author) {

    const card =
        document.createElement("a");

    card.className =
        "author-card";

    card.href =
        `author.html?id=${encodeURIComponent(
            author.id
        )}`;


    /* ========================================
       Avatar
    ======================================== */

    const avatarContainer =
        document.createElement("div");

    avatarContainer.className =
        "author-card-avatar";


    if (author.avatar) {

        const avatar =
            document.createElement("img");

        avatar.src =
            resolveAssetPath(
                author.avatar
            );

        avatar.alt =
            `${author.name} avatar`;

        avatar.loading =
            "lazy";


        avatar.addEventListener(
            "error",
            () => {

                avatar.remove();

                avatarContainer.innerHTML = `
                    <div class="author-card-placeholder">
                        ?
                    </div>
                `;

            }
        );


        avatarContainer.appendChild(
            avatar
        );

    } else {

        avatarContainer.innerHTML = `
            <div class="author-card-placeholder">
                ?
            </div>
        `;

    }


    /* ========================================
       Content
    ======================================== */

    const content =
        document.createElement("div");

    content.className =
        "author-card-content";


    const name =
        document.createElement("h2");

    name.textContent =
        author.name ||
        "Unknown author";


    const apps =
        getAppsByAuthor(
            author.id
        );


    const count =
        document.createElement("span");

    count.className =
        "author-card-count";

    count.textContent =
        `${apps.length} ${
            apps.length === 1
                ? "Homebrew"
                : "Homebrew"
        }`;


    const bio =
        document.createElement("p");

    bio.className =
        "author-card-bio";

    bio.textContent =
        author.bio ||
        "PlayStation Vita Homebrew developer.";


    content.appendChild(
        name
    );

    content.appendChild(
        bio
    );

    content.appendChild(
        count
    );


    /* ========================================
       Arrow
    ======================================== */

    const arrow =
        document.createElement("span");

    arrow.className =
        "author-card-arrow";

    arrow.textContent =
        "→";


    card.appendChild(
        avatarContainer
    );

    card.appendChild(
        content
    );

    card.appendChild(
        arrow
    );


    return card;

}


/**
 * Renderiza todos los autores.
 */
function renderAuthors(
    authors
) {

    const grid =
        document.getElementById(
            "authors-grid"
        );

    const empty =
        document.getElementById(
            "authors-empty"
        );

    const countElement =
        document.getElementById(
            "authors-count"
        );


    if (!grid) {
        return;
    }


    grid.innerHTML = "";


    /*
     * Sort alphabetically.
     */

    const sortedAuthors =
        [...authors].sort(
            (a, b) =>
                (a.name || "").localeCompare(
                    b.name || "",
                    undefined,
                    {
                        sensitivity: "base"
                    }
                )
        );


    countElement.textContent =
        `${sortedAuthors.length} ${
            sortedAuthors.length === 1
                ? "author"
                : "authors"
        }`;


    if (
        sortedAuthors.length === 0
    ) {

        empty.hidden =
            false;

        return;

    }


    empty.hidden =
        true;


    sortedAuthors.forEach(
        author => {

            const card =
                renderAuthorCard(
                    author
                );

            grid.appendChild(
                card
            );

        }
    );

}


/**
 * Filtra autores.
 */
function filterAuthors() {

    const searchInput =
        document.getElementById(
            "author-search"
        );


    if (!searchInput) {
        return;
    }


    const query =
        searchInput.value
            .trim()
            .toLowerCase();


    const filtered =
        VitaHubData.authors.filter(
            author => {

                const name =
                    (
                        author.name || ""
                    ).toLowerCase();


                const bio =
                    (
                        author.bio || ""
                    ).toLowerCase();


                return (
                    name.includes(query) ||
                    bio.includes(query)
                );

            }
        );


    renderAuthors(
        filtered
    );

}


/**
 * Initialize.
 */
async function initAuthorsPage() {

    try {

        await loadVitaHubData();


        renderAuthors(
            VitaHubData.authors
        );


        const searchInput =
            document.getElementById(
                "author-search"
            );


        if (searchInput) {

            searchInput.addEventListener(
                "input",
                filterAuthors
            );

        }


    } catch (error) {

        console.error(
            "Failed to initialize authors page:",
            error
        );

    }

}


document.addEventListener(
    "DOMContentLoaded",
    initAuthorsPage
);