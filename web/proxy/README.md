# VitaDB download proxy (Cloudflare Worker)

Browsers loading [PSVitaAlive Store](https://vegettosan.github.io/PSVitaAlive/) cannot download VPKs from `rinnegatamante.eu` directly (HTTP 403 anti-hotlink). Icons/screenshots are handled in the frontend; **binary downloads need this worker**.

## What it does

- `GET /?url=<encoded VitaDB URL>`
- Allowed targets only:
  - `https://www.rinnegatamante.eu/vitadb/get_hb_url.php?id=…`
  - `https://www.rinnegatamante.eu/files/vitadb/…`
- Fetches upstream with `Referer: https://www.rinnegatamante.eu/vitadb/`
- Streams the file back with CORS headers

## Deploy (Cloudflare, free tier)

1. Create a Worker in the [Cloudflare dashboard](https://dash.cloudflare.com/) → Workers & Pages → Create.
2. Paste the contents of `vitadb-download-worker.js`.
3. Deploy and copy the URL, e.g. `https://psvitaalive-vitadb-proxy.<subdomain>.workers.dev`.
4. Set that URL in the website config (see below).

### Wrangler (optional)

```bash
cd web/proxy
npx wrangler login
npx wrangler deploy vitadb-download-worker.js --name psvitaalive-vitadb-proxy
```

## Wire the website

In `web/js/config.js`:

```js
window.PSVITAALIVE_CONFIG = {
  vitadbDownloadProxy: "https://psvitaalive-vitadb-proxy.YOUR_SUBDOMAIN.workers.dev"
};
```

`web/index.html` / `web/app.html` must load `config.js` **before** the other scripts (already listed if present).

If `vitadbDownloadProxy` is empty, download buttons for VitaDB fall back to the official info page (`#/info/<id>`) so the user can still obtain the file.

## Security

The worker refuses any host other than `rinnegatamante.eu` and only the download paths above (not an open proxy).
