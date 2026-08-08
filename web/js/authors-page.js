/* ========================================
   Authors Page
======================================== */

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


/*
 * Resolve author avatar.
 *
 * GitHub profile images stored as:
 * https://github.com/USERNAME.png
 * are converted to the avatar CDN form.
 *
 * This avoids relying on the GitHub profile
 * page redirect when the browser is loading
 * many author cards at once.
 */
function resolveAuthorAvatar(
    author
) {

    const avatar =
        author.avatar;

    if (!avatar) {
        return null;
    }

    if (
        /^https?:\/\/github\.com\/[^/]+\.png(?:\?.*)?$/i.test(
            avatar
        )
    ) {

        const username =
            avatar
                .replace(
                    /^https?:\/\/github\.com\//i,
                    ""
                )
                .replace(
                    /\.png(?:\?.*)?$/i,
                    ""
                );

        return (
            `https://avatars.githubusercontent.com/${encodeURIComponent(
                username
            )}?size=256`
        );
    }

    return resolveAssetPath(
        avatar
    );
}


function renderAuthorCard(
    author
) {

    const card =
        document.createElement(
            "a"
        );

    card.className =
        "author-card";

    card.href =
        `author.html?id=${encodeURIComponent(
            author.id
        )}`;


    const avatarContainer =
        document.createElement(
            "div"
        );

    avatarContainer.className =
        "author-card-avatar";


    const avatarUrl =
        resolveAuthorAvatar(
            author
        );


    if (avatarUrl) {

        const avatar =
            document.createElement(
                "img"
            );

        avatar.src =
            avatarUrl;

        avatar.alt =
            `${author.name} avatar`;

        avatar.loading =
            "lazy";

        avatar.decoding =
            "async";


        avatar.addEventListener(
            "error",
            () => {

                /*
                 * One fallback to the original
                 * URL before showing placeholder.
                 */

                const original =
                    author.avatar;

                if (
                    original &&
                    avatar.src !== original
                ) {

                    avatar.src =
                        original;

                    return;
                }

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


    const content =
        document.createElement(
            "div"
        );

    content.className =
        "author-card-content";


    const name =
        document.createElement(
            "h2"
        );

    name.textContent =
        author.name ||
        "Unknown author";


    const apps =
        getAppsByAuthor(
            author.id
        );


    const count =
        document.createElement(
            "span"
        );

    count.className =
        "author-card-count";

    count.textContent =
        `${apps.length} Homebrew`;


    const bio =
        document.createElement(
            "p"
        );

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


    const arrow =
        document.createElement(
            "span"
        );

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


    grid.innerHTML =
        "";


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


    if (countElement) {
        countElement.textContent =
            `${sortedAuthors.length} ${
                sortedAuthors.length === 1
                    ? "author"
                    : "authors"
            }`;
    }


    if (
        sortedAuthors.length === 0
    ) {

        if (empty) {
            empty.hidden =
                false;
        }

        return;
    }


    if (empty) {
        empty.hidden =
            true;
    }


    sortedAuthors.forEach(
        author => {

            grid.appendChild(
                renderAuthorCard(
                    author
                )
            );

        }
    );
}


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
                        author.name ||
                        ""
                    ).toLowerCase();

                const bio =
                    (
                        author.bio ||
                        ""
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
