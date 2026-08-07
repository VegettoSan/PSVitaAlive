/* ========================================
   Category Page
======================================== */

function resolveCategoryIconPath(
    path
) {

    if (!path) {
        return null;
    }


    /*
     * External URLs
     */

    if (
        path.startsWith("http://") ||
        path.startsWith("https://") ||
        path.startsWith("data:") ||
        path.startsWith("/")
    ) {

        return path;

    }


    /*
     * Category icons are stored in:
     *
     * categories/icons/
     */

    return `../categories/${path}`;

}

function getCategoryPageParams() {

    const params =
        new URLSearchParams(
            window.location.search
        );


    return {

        category:
            params.get("id"),

        subcategory:
            params.get("subcategory")

    };

}


/* ========================================
   Sort Homebrew
======================================== */


function sortHomebrewByDate(
    apps
) {

    return [...apps].sort(
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

}


/* ========================================
   Render all categories
======================================== */


function renderAllCategories() {

    const section =
        document.getElementById(
            "all-categories-section"
        );

    const grid =
        document.getElementById(
            "categories-grid"
        );


    grid.innerHTML = "";


    const categories =
        [...VitaHubData.categories]
            .sort(
                (a, b) =>
                    Number(a.order || 999) -
                    Number(b.order || 999)
            );


    categories.forEach(
        category => {

            const card =
                document.createElement(
                    "a"
                );


            card.className =
                "category-browser-card";


            card.href =
                `category.html?id=${encodeURIComponent(
                    category.id
                )}`;


            /*
             * Icon
             */

            const iconContainer =
                document.createElement(
                    "div"
                );

            iconContainer.className =
                "category-browser-icon";


            if (
                category.icon
            ) {

                const icon =
                    document.createElement(
                        "img"
                    );

                icon.src =
    resolveCategoryIconPath(
        category.icon
    );

                icon.alt =
                    `${category.name} icon`;

                icon.loading =
                    "lazy";


                icon.addEventListener(
                    "error",
                    () => {

                        icon.remove();

                    }
                );


                iconContainer.appendChild(
                    icon
                );

            }


            /*
             * Content
             */

            const content =
                document.createElement(
                    "div"
                );


            const title =
                document.createElement(
                    "h2"
                );

            title.textContent =
                category.name;


            const description =
                document.createElement(
                    "p"
                );

            description.textContent =
                category.description ||
                "No description available.";


            const count =
                document.createElement(
                    "span"
                );

            count.className =
                "category-browser-count";


            const appCount =
                VitaHubData.catalog.filter(
                    app =>
                        app.category_id ===
                        category.id
                ).length;


            count.textContent =
                `${appCount} Homebrew`;


            content.appendChild(
                title
            );

            content.appendChild(
                description
            );

            content.appendChild(
                count
            );


            card.appendChild(
                iconContainer
            );

            card.appendChild(
                content
            );


            grid.appendChild(
                card
            );

        }
    );


    section.hidden = false;

}


/* ========================================
   Render category header
======================================== */


function renderCategoryHeader(
    category
) {

    const header =
        document.getElementById(
            "category-header"
        );


    header.innerHTML = "";


    const icon =
        document.createElement(
            "div"
        );

    icon.className =
        "category-detail-icon";


    if (
        category.icon
    ) {

        const image =
            document.createElement(
                "img"
            );

        image.src =
    resolveCategoryIconPath(
        category.icon
    );

        image.alt =
            `${category.name} icon`;

        image.loading =
            "eager";


        icon.appendChild(
            image
        );

    }


    const content =
        document.createElement(
            "div"
        );


    const label =
        document.createElement(
            "span"
        );

    label.className =
        "hero-label";

    label.textContent =
        "Category";


    const title =
        document.createElement(
            "h1"
        );

    title.textContent =
        category.name;


    const description =
        document.createElement(
            "p"
        );

    description.textContent =
        category.description ||
        "No description available.";


    content.appendChild(
        label
    );

    content.appendChild(
        title
    );

    content.appendChild(
        description
    );


    header.appendChild(
        icon
    );

    header.appendChild(
        content
    );

}


/* ========================================
   Render subcategories
======================================== */


function renderSubcategories(
    category,
    activeSubcategory
) {

    const section =
        document.getElementById(
            "subcategory-section"
        );

    const container =
        document.getElementById(
            "subcategories"
        );


    container.innerHTML = "";


    const subcategories =
        Array.isArray(
            category.subcategories
        )
            ? category.subcategories
            : [];


    if (
        subcategories.length === 0
    ) {

        section.hidden = true;

        return;

    }


    section.hidden = false;


    /*
     * All button
     */

    const allLink =
        document.createElement(
            "a"
        );


    allLink.className =
        "subcategory-button";


    if (
        !activeSubcategory
    ) {

        allLink.classList.add(
            "active"
        );

    }


    allLink.href =
        `category.html?id=${encodeURIComponent(
            category.id
        )}`;


    allLink.textContent =
        "All";


    container.appendChild(
        allLink
    );


    /*
     * Subcategories
     */

    subcategories.forEach(
        subcategory => {

            const link =
                document.createElement(
                    "a"
                );


            link.className =
                "subcategory-button";


            if (
                activeSubcategory ===
                subcategory.id
            ) {

                link.classList.add(
                    "active"
                );

            }


            link.href =
                `category.html?id=${encodeURIComponent(
                    category.id
                )}&subcategory=${encodeURIComponent(
                    subcategory.id
                )}`;


            link.textContent =
                subcategory.name;


            container.appendChild(
                link
            );

        }
    );

}


/* ========================================
   Render category applications
======================================== */


function renderCategoryApps(
    categoryId,
    subcategoryId
) {

    const grid =
        document.getElementById(
            "category-apps-grid"
        );


    const empty =
        document.getElementById(
            "category-empty"
        );


    const description =
        document.getElementById(
            "category-apps-description"
        );


    let apps =
        VitaHubData.catalog.filter(
            app =>
                app.category_id ===
                categoryId
        );


    if (
        subcategoryId
    ) {

        apps =
            apps.filter(
                app =>
                    Array.isArray(
                        app.subcategory_ids
                    ) &&
                    app.subcategory_ids.includes(
                        subcategoryId
                    )
            );

    }


    apps =
        sortHomebrewByDate(
            apps
        );


    grid.innerHTML = "";


    if (
        apps.length === 0
    ) {

        empty.hidden = false;

        description.textContent =
            "No Homebrew found.";

        return;

    }


    empty.hidden = true;


    description.textContent =
        `${apps.length} Homebrew`;


    apps.forEach(
        app => {

            const card =
                renderAppCard(
                    app
                );


            grid.appendChild(
                card
            );

        }
    );

}


/* ========================================
   Render category
======================================== */


function renderCategoryPage(
    categoryId,
    subcategoryId
) {

    const category =
        getCategoryById(
            categoryId
        );


    if (!category) {

        throw new Error(
            `Category not found: ${categoryId}`
        );

    }


    document.title =
        `${category.name} - PSVitaAlive Store`;


    renderCategoryHeader(
        category
    );


    renderSubcategories(
        category,
        subcategoryId
    );


    renderCategoryApps(
        categoryId,
        subcategoryId
    );


    document
        .getElementById(
            "category-detail-section"
        )
        .hidden = false;

}


/* ========================================
   Initialize
======================================== */


async function initCategoryPage() {

    const loading =
        document.getElementById(
            "category-loading"
        );

    const error =
        document.getElementById(
            "category-error"
        );


    try {

        await loadVitaHubData();


        const {
            category,
            subcategory
        } =
            getCategoryPageParams();


        loading.hidden = true;


        if (
            category
        ) {

            renderCategoryPage(
                category,
                subcategory
            );

        } else {

            renderAllCategories();

        }

    } catch (
        errorObject
    ) {

        console.error(
            "Failed to initialize category page:",
            errorObject
        );


        loading.hidden = true;

        error.hidden = false;

        error.textContent =
            errorObject.message ||
            "Failed to load categories.";

    }

}


document.addEventListener(
    "DOMContentLoaded",
    initCategoryPage
);