/**
 * Runtime config for the static store website.
 * Deploy the Worker in web/proxy/ then set vitadbDownloadProxy to its URL.
 */
window.PSVITAALIVE_CONFIG = window.PSVITAALIVE_CONFIG || {
    /**
     * Cloudflare Worker base URL (no trailing slash), e.g.
     * "https://psvitaalive-vitadb-proxy.example.workers.dev"
     * Leave empty until the worker is deployed.
     */
    vitadbDownloadProxy: ""
};
