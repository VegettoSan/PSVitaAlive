async function initHomePage() {
    try {
        const data = await loadVitaHubData();

        const appsGrid = document.getElementById(
            "latest-apps-grid"
        );

        if (!appsGrid) {
            return;
        }

        appsGrid.innerHTML = "";

        data.catalog.forEach(app => {
            const card = renderAppCard(app);

            appsGrid.appendChild(card);
        });

    } catch (error) {
        console.error(
            "Failed to initialize VitaHub:",
            error
        );
    }
}


document.addEventListener("DOMContentLoaded", () => {
    initHomePage();
});