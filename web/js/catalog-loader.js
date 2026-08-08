/* ========================================
   PSVitaAlive Catalog Loader
======================================== */

(function () {

    let loaderElement = null;
    let loaderTitle = null;
    let loaderStatus = null;
    let loaderBar = null;
    let loaderPercent = null;

    const activeLoads = new Map();

    function getLoaderLabel(path) {

        const value = String(path || "");

        if (value.includes("catalog_psp_games.json")) {
            return "Loading PSP Games...";
        }

        if (value.includes("catalog_ps1_games.json")) {
            return "Loading PS1 Games...";
        }

        if (value.includes("catalog_psvita_games.json")) {
            return "Loading PS Vita Games...";
        }

        if (value.includes("catalog.json")) {
            return "Loading Homebrew...";
        }

        if (value.includes("authors.json")) {
            return "Loading Authors...";
        }

        if (value.includes("categories.json")) {
            return "Loading Categories...";
        }

        return "Loading catalog...";
    }

    function injectLoaderStyles() {

        if (document.getElementById("psvitaalive-loader-styles")) {
            return;
        }

        const style = document.createElement("style");
        style.id = "psvitaalive-loader-styles";

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
                border: 1px solid rgba(255, 255, 255, 0.08);
                border-radius: 20px;
                background: rgba(255, 255, 255, 0.035);
                box-shadow: 0 18px 50px rgba(0, 0, 0, 0.22);
                text-align: center;
                backdrop-filter: blur(12px);
            }

            .psvitaalive-loader-kicker {
                display: block;
                margin-bottom: 8px;
                font-size: 0.72rem;
                font-weight: 800;
                letter-spacing: 0.16em;
                text-transform: uppercase;
                opacity: 0.7;
            }

            .psvitaalive-loader-title {
                margin: 0;
                font-size: clamp(1.15rem, 2vw, 1.45rem);
                font-weight: 800;
            }

            .psvitaalive-loader-status {
                margin: 8px 0 20px;
                font-size: 0.9rem;
                opacity: 0.7;
            }

            .psvitaalive-loader-track {
                width: 100%;
                height: 10px;
                overflow: hidden;
                border-radius: 999px;
                background: rgba(255, 255, 255, 0.09);
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

            .psvitaalive-loader-meta {
                display: flex;
                align-items: center;
                justify-content: space-between;
                gap: 12px;
                margin-top: 10px;
                font-size: 0.78rem;
                font-weight: 700;
                opacity: 0.72;
            }

            .psvitaalive-loader-indeterminate {
                position: relative;
                overflow: hidden;
            }

            .psvitaalive-loader-indeterminate::after {
                content: "";
                position: absolute;
                top: 0;
                left: -35%;
                width: 35%;
                height: 100%;
                border-radius: inherit;
                background: rgba(255, 255, 255, 0.35);
                animation: psvitaalive-loader-slide 1.15s ease-in-out infinite;
            }

            @keyframes psvitaalive-loader-slide {
                0% {
                    left: -35%;
                }
                100% {
                    left: 100%;
                }
            }

            @media (prefers-reduced-motion: reduce) {
                .psvitaalive-loader-indeterminate::after {
                    animation: none;
                    left: 0;
                    width: 100%;
                    opacity: 0.18;
                }
            }
        `;

        document.head.appendChild(style);
    }

    function ensureLoader() {

        injectLoaderStyles();

        const grid = document.getElementById("latest-apps-grid");

        if (!grid) {
            return false;
        }

        grid.classList.add("psvitaalive-loading-grid");

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

                    <div class="psvitaalive-loader-track">
                        <div class="psvitaalive-loader-bar"></div>
                    </div>

                    <div class="psvitaalive-loader-meta">
                        <span>Downloading</span>
                        <span class="psvitaalive-loader-percent">
                            0%
                        </span>
                    </div>
                </div>
            </div>
        `;

        loaderElement =
            grid.querySelector(".psvitaalive-catalog-loader");

        loaderTitle =
            grid.querySelector(".psvitaalive-loader-title");

        loaderStatus =
            grid.querySelector(".psvitaalive-loader-status");

        loaderBar =
            grid.querySelector(".psvitaalive-loader-bar");

        loaderPercent =
            grid.querySelector(".psvitaalive-loader-percent");

        return true;
    }

    function getAverageProgress() {

        const values = [...activeLoads.values()];

        if (!values.length) {
            return 0;
        }

        return Math.round(
            values.reduce(
                (total, value) => total + value,
                0
            ) / values.length
        );
    }

    function updateLoaderProgress() {

        if (!loaderElement || !loaderBar) {
            return;
        }

        const progress = getAverageProgress();

        loaderBar.style.width = `${progress}%`;

        if (loaderPercent) {
            loaderPercent.textContent = `${progress}%`;
        }
    }

    window.showPSVitaAliveLoader = function (label) {

        if (!ensureLoader()) {
            return;
        }

        if (loaderTitle) {
            loaderTitle.textContent =
                label || "Loading catalog...";
        }

        if (loaderStatus) {
            loaderStatus.textContent =
                "Downloading catalog...";
        }

        if (loaderBar) {
            loaderBar.style.width = "0%";
        }

        if (loaderPercent) {
            loaderPercent.textContent = "0%";
        }
    };

    window.updatePSVitaAliveLoaderStatus = function (
        status
    ) {

        if (loaderStatus) {
            loaderStatus.textContent =
                status || "Processing data...";
        }
    };

    window.hidePSVitaAliveLoader = function () {

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

    async function loadJSONWithProgress(path) {

        const label =
            getLoaderLabel(path);

        const loadId =
            `${path}-${Date.now()}-${Math.random()}`;

        activeLoads.set(
            loadId,
            0
        );

        if (activeLoads.size === 1) {
            window.showPSVitaAliveLoader(label);
        }

        try {

            const response =
                await fetch(path);

            if (!response.ok) {
                throw new Error(
                    `No se pudo cargar ${path}: ${response.status} ${response.statusText}`
                );
            }

            const total =
                Number(
                    response.headers.get(
                        "content-length"
                    )
                );

            if (
                !response.body ||
                !Number.isFinite(total) ||
                total <= 0
            ) {

                window.updatePSVitaAliveLoaderStatus(
                    "Loading catalog data..."
                );

                const data =
                    await response.json();

                activeLoads.set(
                    loadId,
                    100
                );

                updateLoaderProgress();

                return data;
            }

            const reader =
                response.body.getReader();

            const chunks = [];
            let received = 0;

            while (true) {

                const {
                    done,
                    value
                } =
                    await reader.read();

                if (done) {
                    break;
                }

                chunks.push(value);
                received += value.byteLength;

                const progress =
                    Math.min(
                        100,
                        Math.round(
                            (received / total) * 100
                        )
                    );

                activeLoads.set(
                    loadId,
                    progress
                );

                updateLoaderProgress();
            }

            window.updatePSVitaAliveLoaderStatus(
                "Processing catalog data..."
            );

            const combined =
                new Uint8Array(
                    received
                );

            let offset = 0;

            for (const chunk of chunks) {
                combined.set(
                    chunk,
                    offset
                );

                offset += chunk.length;
            }

            const text =
                new TextDecoder(
                    "utf-8"
                ).decode(
                    combined
                );

            return JSON.parse(text);

        } finally {

            activeLoads.delete(
                loadId
            );

            updateLoaderProgress();

            if (
                activeLoads.size === 0
            ) {

                window.updatePSVitaAliveLoaderStatus(
                    "Ready"
                );

                if (loaderBar) {
                    loaderBar.style.width = "100%";
                }

                if (loaderPercent) {
                    loaderPercent.textContent = "100%";
                }

                setTimeout(
                    () => {
                        window.hidePSVitaAliveLoader();
                    },
                    120
                );
            }
        }
    }

    /*
     * Replace the existing global loader while keeping
     * the same loadJSON(path) API used by the project.
     */

    window.loadJSON =
        loadJSONWithProgress;

})();
