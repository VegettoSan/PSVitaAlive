/* ========================================
   PSVitaAlive Catalog Loader
======================================== */

(function () {

    let loaderElement = null;
    let loaderTitle = null;
    let loaderStatus = null;
    let loaderBar = null;
    let loaderPercent = null;

    function injectLoaderStyles() {

        if (
            document.getElementById(
                "psvitaalive-loader-styles"
            )
        ) {
            return;
        }

        const style =
            document.createElement(
                "style"
            );

        style.id =
            "psvitaalive-loader-styles";

        style.textContent = `
            #latest-apps-grid.psvitaalive-loading-grid {
                display: grid;
                grid-template-columns: 1fr;
                width: 100%;
            }

            .psvitaalive-catalog-loader {
                grid-column: 1 / -1;
                width: 100%;
                min-height: 240px;
                display: flex;
                align-items: center;
                justify-content: center;
                padding: 36px 20px;
                box-sizing: border-box;
            }

            .psvitaalive-catalog-loader-card {
                width: min(560px, 100%);
                padding: 30px 28px;
                border: 1px solid rgba(255,255,255,.08);
                border-radius: 20px;
                background: rgba(255,255,255,.035);
                box-shadow: 0 18px 50px rgba(0,0,0,.22);
                text-align: center;
                backdrop-filter: blur(12px);
            }

            .psvitaalive-loader-kicker {
                display: block;
                margin-bottom: 8px;
                font-size: .72rem;
                font-weight: 800;
                letter-spacing: .16em;
                text-transform: uppercase;
                opacity: .7;
            }

            .psvitaalive-loader-title {
                margin: 0;
                font-size: clamp(1.15rem,2vw,1.45rem);
                font-weight: 800;
            }

            .psvitaalive-loader-status {
                margin: 8px 0 20px;
                font-size: .9rem;
                opacity: .7;
            }

            .psvitaalive-loader-track {
                position: relative;
                width: 100%;
                height: 10px;
                overflow: hidden;
                border-radius: 999px;
                background: rgba(255,255,255,.09);
            }

            .psvitaalive-loader-bar {
                width: 0%;
                height: 100%;
                border-radius: inherit;
                background: linear-gradient(
                    90deg,
                    #66e3a4,
                    #8cf0bd
                );
                transition: width 160ms ease;
            }

            .psvitaalive-loader-track.indeterminate
            .psvitaalive-loader-bar {
                width: 38% !important;
                animation:
                    psvitaalive-loader-slide
                    1.15s
                    ease-in-out
                    infinite;
            }

            @keyframes psvitaalive-loader-slide {
                0% {
                    transform: translateX(-130%);
                }
                100% {
                    transform: translateX(360%);
                }
            }

            .psvitaalive-loader-meta {
                display: flex;
                align-items: center;
                justify-content: space-between;
                gap: 12px;
                margin-top: 10px;
                font-size: .78rem;
                font-weight: 700;
                opacity: .72;
            }

            @media (prefers-reduced-motion: reduce) {
                .psvitaalive-loader-track.indeterminate
                .psvitaalive-loader-bar {
                    animation: none;
                    transform: none;
                    width: 100% !important;
                    opacity: .25;
                }
            }
        `;

        document.head.appendChild(
            style
        );
    }


    function ensureLoader() {

        injectLoaderStyles();

        const grid =
            document.getElementById(
                "latest-apps-grid"
            );

        if (!grid) {
            return false;
        }

        grid.classList.add(
            "psvitaalive-loading-grid"
        );

        grid.innerHTML = `
            <div class="psvitaalive-catalog-loader">
                <div class="psvitaalive-catalog-loader-card">
                    <span class="psvitaalive-loader-kicker">
                        PSVitaAlive
                    </span>

                    <h3 class="psvitaalive-loader-title">
                        Loading catalog...
                    </h3>

                    <p class="psvitaalive-loader-status">
                        Preparing data...
                    </p>

                    <div class="psvitaalive-loader-track indeterminate">
                        <div class="psvitaalive-loader-bar"></div>
                    </div>

                    <div class="psvitaalive-loader-meta">
                        <span>Downloading</span>
                        <span class="psvitaalive-loader-percent">
                            --
                        </span>
                    </div>
                </div>
            </div>
        `;

        loaderElement =
            grid.querySelector(
                ".psvitaalive-catalog-loader"
            );

        loaderTitle =
            grid.querySelector(
                ".psvitaalive-loader-title"
            );

        loaderStatus =
            grid.querySelector(
                ".psvitaalive-loader-status"
            );

        loaderBar =
            grid.querySelector(
                ".psvitaalive-loader-bar"
            );

        loaderPercent =
            grid.querySelector(
                ".psvitaalive-loader-percent"
            );

        return true;
    }


    window.showPSVitaAliveLoader =
        function(label) {

            if (!ensureLoader()) {
                return;
            }

            if (loaderTitle) {
                loaderTitle.textContent =
                    label ||
                    "Loading catalog...";
            }

            if (loaderStatus) {
                loaderStatus.textContent =
                    "Connecting...";
            }

            if (loaderBar) {
                loaderBar.style.width =
                    "38%";
            }

            const track =
                loaderBar
                    ?.parentElement;

            if (track) {
                track.classList.add(
                    "indeterminate"
                );
            }

            if (loaderPercent) {
                loaderPercent.textContent =
                    "--";
            }
        };


    window.setPSVitaAliveLoaderProgress =
        function(progress) {

            if (
                !loaderBar ||
                !loaderElement
            ) {
                return;
            }

            const value =
                Math.max(
                    0,
                    Math.min(
                        100,
                        Number(progress) || 0
                    )
                );

            const track =
                loaderBar.parentElement;

            if (track) {
                track.classList.remove(
                    "indeterminate"
                );
            }

            loaderBar.style.width =
                `${value}%`;

            if (loaderPercent) {
                loaderPercent.textContent =
                    `${value}%`;
            }
        };


    window.updatePSVitaAliveLoaderStatus =
        function(status) {

            if (loaderStatus) {
                loaderStatus.textContent =
                    status ||
                    "Processing data...";
            }
        };


    window.hidePSVitaAliveLoader =
        function() {

            if (loaderElement) {
                loaderElement.remove();
            }

            const grid =
                document.getElementById(
                    "latest-apps-grid"
                );

            if (grid) {
                grid.classList.remove(
                    "psvitaalive-loading-grid"
                );
            }

            loaderElement = null;
            loaderTitle = null;
            loaderStatus = null;
            loaderBar = null;
            loaderPercent = null;
        };


    /*
     * Keep the project's loadJSON API intact.
     * This loader is used for Homebrew/authors/categories.
     * Game catalogs use their own direct loader in
     * catalog-switcher.js so they cannot race this lifecycle.
     */

    async function loadJSONWithProgress(path) {

        const response =
            await fetch(path);

        if (!response.ok) {
            throw new Error(
                `No se pudo cargar ${path}: ${response.status} ${response.statusText}`
            );
        }

        return response.json();
    }

    window.loadJSON =
        loadJSONWithProgress;

})();
