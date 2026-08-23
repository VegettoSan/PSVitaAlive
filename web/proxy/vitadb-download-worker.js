/**
 * PSVitaAlive — VitaDB download proxy (Cloudflare Worker)
 *
 * Browsers on other origins get HTTP 403 from rinnegatamante.eu (anti-hotlink).
 * This worker fetches allowed VitaDB download URLs with a VitaDB Referer and
 * streams the response to the client.
 *
 * Deploy: see web/proxy/README.md
 * Endpoint: GET /?url=<encoded https://www.rinnegatamante.eu/...>
 */

const ALLOWED_HOSTS = new Set([
  "www.rinnegatamante.eu",
  "rinnegatamante.eu",
]);

function corsHeaders(request) {
  const origin = request.headers.get("Origin") || "*";
  return {
    "Access-Control-Allow-Origin": origin,
    "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, Range",
    "Access-Control-Expose-Headers": "Content-Length, Content-Type, Content-Disposition",
    "Vary": "Origin",
  };
}

function isAllowedTarget(parsed) {
  if (!ALLOWED_HOSTS.has(parsed.hostname)) return false;
  const path = parsed.pathname || "";
  // Official download gateway + hosted VPK/data under files/vitadb/
  if (path === "/vitadb/get_hb_url.php" || path.startsWith("/vitadb/get_hb_url.php")) return true;
  if (path.startsWith("/files/vitadb/")) return true;
  return false;
}

function filenameFromUrl(parsed, contentDisposition) {
  if (contentDisposition) {
    const m = /filename\*?=(?:UTF-8''|")?([^";]+)/i.exec(contentDisposition);
    if (m) {
      try { return decodeURIComponent(m[1].replace(/"/g, "").trim()); } catch (_) {}
      return m[1].replace(/"/g, "").trim();
    }
  }
  const base = parsed.pathname.split("/").pop() || "download.vpk";
  if (base.includes("get_hb_url")) return "download.vpk";
  return base || "download.vpk";
}

export default {
  async fetch(request) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders(request) });
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("Method not allowed", { status: 405, headers: corsHeaders(request) });
    }

    const reqUrl = new URL(request.url);
    const target = reqUrl.searchParams.get("url");
    if (!target) {
      return new Response("Missing url query parameter", { status: 400, headers: corsHeaders(request) });
    }

    let parsed;
    try {
      parsed = new URL(target);
    } catch (_) {
      return new Response("Invalid url", { status: 400, headers: corsHeaders(request) });
    }

    if (parsed.protocol !== "https:" && parsed.protocol !== "http:") {
      return new Response("Unsupported protocol", { status: 400, headers: corsHeaders(request) });
    }

    if (!isAllowedTarget(parsed)) {
      return new Response("URL host/path not allowed", { status: 403, headers: corsHeaders(request) });
    }

    const upstreamHeaders = {
      "User-Agent":
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
      "Accept": "*/*",
      "Referer": "https://www.rinnegatamante.eu/vitadb/",
      "Origin": "https://www.rinnegatamante.eu",
    };

    const range = request.headers.get("Range");
    if (range) upstreamHeaders["Range"] = range;

    let upstream;
    try {
      upstream = await fetch(parsed.toString(), {
        method: request.method,
        headers: upstreamHeaders,
        redirect: "follow",
      });
    } catch (err) {
      return new Response("Upstream fetch failed: " + String(err), {
        status: 502,
        headers: corsHeaders(request),
      });
    }

    const outHeaders = new Headers(corsHeaders(request));
    const ct = upstream.headers.get("Content-Type");
    if (ct) outHeaders.set("Content-Type", ct);
    const cl = upstream.headers.get("Content-Length");
    if (cl) outHeaders.set("Content-Length", cl);
    const cr = upstream.headers.get("Content-Range");
    if (cr) outHeaders.set("Content-Range", cr);
    const acceptRanges = upstream.headers.get("Accept-Ranges");
    if (acceptRanges) outHeaders.set("Accept-Ranges", acceptRanges);

    const cd = upstream.headers.get("Content-Disposition");
    const fname = filenameFromUrl(parsed, cd);
    outHeaders.set(
      "Content-Disposition",
      cd && /filename/i.test(cd) ? cd : `attachment; filename="${fname}"`
    );
    outHeaders.set("Cache-Control", "private, max-age=0");

    return new Response(request.method === "HEAD" ? null : upstream.body, {
      status: upstream.status,
      statusText: upstream.statusText,
      headers: outHeaders,
    });
  },
};
