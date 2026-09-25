from pathlib import Path

CPP = Path('Client PSVitaAlive/source/network/http_client.cpp')
TLS = Path('docs/NETWORK_TLS.md')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f'pattern not found: {label}')
    return text.replace(old, new, 1)


text = CPP.read_text(encoding='utf-8')

text = replace_once(
    text,
    '''static bool hostLooksLikeBadArchiveEdge(const char* url) {
    if (!url || !url[0]) return false;
    // Canadian / dn* storage nodes frequently fail TLS with OpenSSL 1.0.2 on Vita.
    if (containsAsciiNoCase(url, ".ca.archive.org")) return true;
    const char* host = std::strstr(url, "://");
    host = host ? host + 3 : url;
    if (asciiLower(host[0]) == 'd' && asciiLower(host[1]) == 'n' &&
        containsAsciiNoCase(host, "archive.org")) return true;
    return false;
}
''',
    '''static bool hostLooksLikeBadArchiveEdge(const char* url) {
    if (!url || !url[0]) return false;
    // Historical risk marker only: dn*/.ca nodes are no longer hard-filtered.
    // The selector probes them and lets real Vita-side results decide.
    if (containsAsciiNoCase(url, ".ca.archive.org")) return true;
    const char* host = std::strstr(url, "://");
    host = host ? host + 3 : url;
    if (asciiLower(host[0]) == 'd' && asciiLower(host[1]) == 'n' &&
        containsAsciiNoCase(host, "archive.org")) return true;
    return false;
}

static std::string archiveUrlHostLower(const std::string& url) {
    if (url.empty()) return {};
    size_t start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    if (start >= url.size()) return {};
    size_t end = start;
    while (end < url.size() && url[end] != '/' && url[end] != ':' &&
           url[end] != '?' && url[end] != '#') {
        ++end;
    }
    if (end <= start) return {};
    std::string host = url.substr(start, end - start);
    for (char& c : host) c = asciiLower(c);
    return host;
}

static bool archiveEffectiveUrlUsable(const std::string& url) {
    const std::string host = archiveUrlHostLower(url);
    if (host == "archive.org") return true;
    static const char* suffix = ".archive.org";
    const size_t suffixLen = std::strlen(suffix);
    return host.size() > suffixLen &&
           host.compare(host.size() - suffixLen, suffixLen, suffix) == 0;
}
''',
    'risk helper + effective host parser')

text = replace_once(
    text,
    '''// Prefer us.archive.org / ia* nodes over dn*/ca edges for Vita OpenSSL 1.0.2.
static bool buildArchiveAlternateUrls''',
    '''// Keep dn*/.ca Archive edges as lower-priority risk candidates instead of
// filtering them out. Large payloads benchmark every working candidate, so a
// risky edge can win when it is actually faster from the user's own network.
static bool buildArchiveAlternateUrls''',
    'candidate builder comment')

text = replace_once(
    text,
    '''    if (!server.empty() && hostLooksLikeBadArchiveEdge(server.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_FILTERED host=") + server + " reason=dn_or_ca_edge");
    if (!d1.empty() && hostLooksLikeBadArchiveEdge(d1.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_FILTERED host=") + d1 + " reason=dn_or_ca_edge");
    if (!d2.empty() && hostLooksLikeBadArchiveEdge(d2.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_FILTERED host=") + d2 + " reason=dn_or_ca_edge");
''',
    '''    if (!server.empty() && hostLooksLikeBadArchiveEdge(server.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_RISKY host=") + server + " reason=dn_or_ca_edge allowed=1");
    if (!d1.empty() && hostLooksLikeBadArchiveEdge(d1.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_RISKY host=") + d1 + " reason=dn_or_ca_edge allowed=1");
    if (!d2.empty() && hostLooksLikeBadArchiveEdge(d2.c_str())) archiveNodeDiagnostic(std::string("METADATA_HOST_RISKY host=") + d2 + " reason=dn_or_ca_edge allowed=1");
''',
    'risky metadata logging')

text = replace_once(
    text,
    '''    for (const auto& host : hosts) {
        if (hostLooksLikeBadArchiveEdge(host.c_str())) continue;
        std::string u = "https://" + host + dir + "/" + file;
        outUrls.push_back(u);
    }
''',
    '''    for (const auto& host : hosts) {
        // Risky dn*/.ca nodes stay after normal ia* candidates in discovery order,
        // but they are allowed into the probe set and may win a large-file benchmark.
        std::string u = "https://" + host + dir + "/" + file;
        outUrls.push_back(u);
    }
''',
    'allow risky candidates')

text = replace_once(
    text,
    '''            sceClibSnprintf(candidateMsg, sizeof(candidateMsg), "CANDIDATE[%u] url=%s", (unsigned int)i, outUrls[i].c_str());
''',
    '''            sceClibSnprintf(candidateMsg, sizeof(candidateMsg), "CANDIDATE[%u] risk=%s url=%s",
                (unsigned int)i,
                hostLooksLikeBadArchiveEdge(outUrls[i].c_str()) ? "dn_or_ca" : "normal",
                outUrls[i].c_str());
''',
    'candidate risk log')

text = replace_once(
    text,
    '''    uint64_t bytesPerSecond = 0;
    uint64_t elapsedUs = 0;
''',
    '''    uint64_t bytesPerSecond = 0;      // whole request, including TTFB/redirect latency
    uint64_t bodyBytesPerSecond = 0;  // payload body only: bytes / (elapsed - TTFB)
    uint64_t elapsedUs = 0;
''',
    'probe body speed field')

text = replace_once(
    text,
    '''    if (elapsedUs > 0 && ctx.bytes > 0) {
        out.bytesPerSecond = (ctx.bytes * 1000000ULL) / elapsedUs;
    }
''',
    '''    if (elapsedUs > 0 && ctx.bytes > 0) {
        out.bytesPerSecond = (ctx.bytes * 1000000ULL) / elapsedUs;
    }
    const uint64_t bodyUs =
        (out.ttfbUs > 0 && out.elapsedUs > out.ttfbUs) ? (out.elapsedUs - out.ttfbUs) : out.elapsedUs;
    if (bodyUs > 0 && ctx.bytes > 0) {
        out.bodyBytesPerSecond = (ctx.bytes * 1000000ULL) / bodyUs;
    }
''',
    'probe body speed calculation')

text = replace_once(
    text,
    '''        "archive selector probe ok=%d curl=%d HTTP=%ld bytes=%llu total=%llu us=%llu speed=%llu url=%s",
''',
    '''        "archive selector probe ok=%d curl=%d HTTP=%ld bytes=%llu total=%llu us=%llu total_speed=%llu body_speed=%llu url=%s",
''',
    'session probe format')

text = replace_once(
    text,
    '''        (unsigned long long)out.elapsedUs,
        (unsigned long long)out.bytesPerSecond,
        url.c_str());
''',
    '''        (unsigned long long)out.elapsedUs,
        (unsigned long long)out.bytesPerSecond,
        (unsigned long long)out.bodyBytesPerSecond,
        url.c_str());
''',
    'session probe args')

text = replace_once(
    text,
    '''        const uint64_t kibPerSecond = out.bytesPerSecond / 1024ULL;
        const uint64_t mibHundredths = (out.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        char detailed[1500];
        sceClibSnprintf(detailed, sizeof(detailed),
            "PROBE requested=%llu ok=%d curl=%d curl_text=%s HTTP=%ld bytes=%llu remote_total=%llu elapsed_ms=%llu ttfb_ms=%llu speed_Bps=%llu speed_KiBps=%llu speed_MiBps=%llu.%02llu redirects=%ld ip=%s requested_url=%s effective_url=%s detail=%s",
            (unsigned long long)requestedBytes, out.ok ? 1 : 0,
            static_cast<int>(rc), curl_easy_strerror(rc), out.status,
            (unsigned long long)out.bytes, (unsigned long long)out.total,
            (unsigned long long)(out.elapsedUs / 1000ULL),
            (unsigned long long)(out.ttfbUs / 1000ULL),
            (unsigned long long)out.bytesPerSecond,
            (unsigned long long)kibPerSecond,
            (unsigned long long)(mibHundredths / 100ULL),
            (unsigned long long)(mibHundredths % 100ULL),
            out.redirectCount,
            out.primaryIp.empty() ? "-" : out.primaryIp.c_str(),
            url.c_str(),
            out.effectiveUrl.empty() ? "-" : out.effectiveUrl.c_str(),
            error[0] ? error : "-");
''',
    '''        const uint64_t totalKibPerSecond = out.bytesPerSecond / 1024ULL;
        const uint64_t totalMibHundredths = (out.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const uint64_t bodyKibPerSecond = out.bodyBytesPerSecond / 1024ULL;
        const uint64_t bodyMibHundredths = (out.bodyBytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const uint64_t bodyUs =
            (out.ttfbUs > 0 && out.elapsedUs > out.ttfbUs) ? (out.elapsedUs - out.ttfbUs) : out.elapsedUs;
        const char* requestedRisk = hostLooksLikeBadArchiveEdge(url.c_str()) ? "dn_or_ca" : "normal";
        const char* effectiveRisk = (!out.effectiveUrl.empty() && hostLooksLikeBadArchiveEdge(out.effectiveUrl.c_str())) ? "dn_or_ca" : "normal";
        char detailed[1800];
        sceClibSnprintf(detailed, sizeof(detailed),
            "PROBE requested=%llu ok=%d curl=%d curl_text=%s HTTP=%ld bytes=%llu remote_total=%llu elapsed_ms=%llu ttfb_ms=%llu body_ms=%llu total_speed_Bps=%llu total_speed_KiBps=%llu total_speed_MiBps=%llu.%02llu body_speed_Bps=%llu body_speed_KiBps=%llu body_speed_MiBps=%llu.%02llu redirects=%ld ip=%s risk_requested=%s risk_effective=%s requested_url=%s effective_url=%s detail=%s",
            (unsigned long long)requestedBytes, out.ok ? 1 : 0,
            static_cast<int>(rc), curl_easy_strerror(rc), out.status,
            (unsigned long long)out.bytes, (unsigned long long)out.total,
            (unsigned long long)(out.elapsedUs / 1000ULL),
            (unsigned long long)(out.ttfbUs / 1000ULL),
            (unsigned long long)(bodyUs / 1000ULL),
            (unsigned long long)out.bytesPerSecond,
            (unsigned long long)totalKibPerSecond,
            (unsigned long long)(totalMibHundredths / 100ULL),
            (unsigned long long)(totalMibHundredths % 100ULL),
            (unsigned long long)out.bodyBytesPerSecond,
            (unsigned long long)bodyKibPerSecond,
            (unsigned long long)(bodyMibHundredths / 100ULL),
            (unsigned long long)(bodyMibHundredths % 100ULL),
            out.redirectCount,
            out.primaryIp.empty() ? "-" : out.primaryIp.c_str(),
            requestedRisk,
            effectiveRisk,
            url.c_str(),
            out.effectiveUrl.empty() ? "-" : out.effectiveUrl.c_str(),
            error[0] ? error : "-");
''',
    'detailed probe metrics')

text = replace_once(
    text,
    '''        if (responsiveIndex == urls.size()) {
            httpDiagnostic("archive selector: no direct node passed validation probe; keep normal archive.org path");
            return false;
        }
        if (responsiveIndex != 0) std::swap(urls[0], urls[responsiveIndex]);
''',
    '''        if (responsiveIndex == urls.size()) {
            httpDiagnostic("archive selector: no direct node passed validation probe; keep normal archive.org path");
            return false;
        }
        if (archiveEffectiveUrlUsable(sizeProbe.effectiveUrl)) {
            archiveNodeDiagnostic(std::string("SMALL_FILE_EFFECTIVE_URL requested=") + urls[responsiveIndex] +
                " effective=" + sizeProbe.effectiveUrl);
            urls[responsiveIndex] = sizeProbe.effectiveUrl;
        }
        if (responsiveIndex != 0) std::swap(urls[0], urls[responsiveIndex]);
''',
    'small file effective URL')

text = replace_once(
    text,
    '''    // Tiny candidate count (normally two): simple stable ordering keeps code small
    // and avoids adding another dependency. Successful nodes come first, then speed.
    for (size_t i = 0; i < probes.size(); ++i) {
        for (size_t j = i + 1; j < probes.size(); ++j) {
            const bool jBetter =
                (probes[j].ok && !probes[i].ok) ||
                (probes[j].ok == probes[i].ok && probes[j].bytesPerSecond > probes[i].bytesPerSecond);
            if (jBetter) std::swap(probes[i], probes[j]);
        }
    }
''',
    '''    // Tiny candidate count (normally two or three): successful nodes first,
    // then payload-body throughput. TTFB is only a tie-breaker so a slow first byte
    // cannot make a fast sustained storage edge look artificially bad on large files.
    for (size_t i = 0; i < probes.size(); ++i) {
        for (size_t j = i + 1; j < probes.size(); ++j) {
            const bool bothOk = probes[j].ok && probes[i].ok;
            const bool jBetter =
                (probes[j].ok && !probes[i].ok) ||
                (bothOk && probes[j].bodyBytesPerSecond > probes[i].bodyBytesPerSecond) ||
                (bothOk && probes[j].bodyBytesPerSecond == probes[i].bodyBytesPerSecond &&
                 probes[j].ttfbUs < probes[i].ttfbUs);
            if (jBetter) std::swap(probes[i], probes[j]);
        }
    }
''',
    'body throughput ranking')

text = replace_once(
    text,
    '''        const uint64_t kibPerSecond = p.bytesPerSecond / 1024ULL;
        const uint64_t mibHundredths = (p.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        char ranked[1200];
        sceClibSnprintf(ranked, sizeof(ranked),
            "RANK[%u] ok=%d speed_Bps=%llu speed_KiBps=%llu speed_MiBps=%llu.%02llu HTTP=%ld curl=%d ttfb_ms=%llu elapsed_ms=%llu redirects=%ld ip=%s url=%s effective=%s",
            (unsigned int)(i + 1), p.ok ? 1 : 0,
            (unsigned long long)p.bytesPerSecond,
            (unsigned long long)kibPerSecond,
            (unsigned long long)(mibHundredths / 100ULL),
            (unsigned long long)(mibHundredths % 100ULL),
            p.status, static_cast<int>(p.curlCode),
            (unsigned long long)(p.ttfbUs / 1000ULL),
            (unsigned long long)(p.elapsedUs / 1000ULL),
            p.redirectCount,
            p.primaryIp.empty() ? "-" : p.primaryIp.c_str(),
            p.url.c_str(),
            p.effectiveUrl.empty() ? "-" : p.effectiveUrl.c_str());
''',
    '''        const uint64_t totalKibPerSecond = p.bytesPerSecond / 1024ULL;
        const uint64_t totalMibHundredths = (p.bytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const uint64_t bodyKibPerSecond = p.bodyBytesPerSecond / 1024ULL;
        const uint64_t bodyMibHundredths = (p.bodyBytesPerSecond * 100ULL) / (1024ULL * 1024ULL);
        const std::string rankedEffective = archiveEffectiveUrlUsable(p.effectiveUrl) ? p.effectiveUrl : p.url;
        char ranked[1500];
        sceClibSnprintf(ranked, sizeof(ranked),
            "RANK[%u] ok=%d body_speed_Bps=%llu body_speed_KiBps=%llu body_speed_MiBps=%llu.%02llu total_speed_Bps=%llu total_speed_KiBps=%llu total_speed_MiBps=%llu.%02llu HTTP=%ld curl=%d ttfb_ms=%llu elapsed_ms=%llu redirects=%ld ip=%s risk=%s requested=%s effective=%s",
            (unsigned int)(i + 1), p.ok ? 1 : 0,
            (unsigned long long)p.bodyBytesPerSecond,
            (unsigned long long)bodyKibPerSecond,
            (unsigned long long)(bodyMibHundredths / 100ULL),
            (unsigned long long)(bodyMibHundredths % 100ULL),
            (unsigned long long)p.bytesPerSecond,
            (unsigned long long)totalKibPerSecond,
            (unsigned long long)(totalMibHundredths / 100ULL),
            (unsigned long long)(totalMibHundredths % 100ULL),
            p.status, static_cast<int>(p.curlCode),
            (unsigned long long)(p.ttfbUs / 1000ULL),
            (unsigned long long)(p.elapsedUs / 1000ULL),
            p.redirectCount,
            p.primaryIp.empty() ? "-" : p.primaryIp.c_str(),
            hostLooksLikeBadArchiveEdge(rankedEffective.c_str()) ? "dn_or_ca" : "normal",
            p.url.c_str(),
            p.effectiveUrl.empty() ? "-" : p.effectiveUrl.c_str());
''',
    'ranking metrics log')

text = replace_once(
    text,
    '''    urls.clear();
    for (const auto& p : probes) urls.push_back(p.url);

    char chosen[420];
    sceClibSnprintf(chosen, sizeof(chosen),
        "archive selector chose speed=%llu B/s total=%llu -> %s",
        (unsigned long long)probes.front().bytesPerSecond,
        (unsigned long long)probes.front().total,
        probes.front().url.c_str());
    httpDiagnostic(chosen);
    archiveNodeDiagnostic(std::string("SELECTED ") + chosen);
    return probes.front().ok;
''',
    '''    // Use what libcurl actually reached, not merely the requested storage URL.
    // Archive can redirect ia601 -> ia801 (and vice versa); dedupe by effective host
    // so the failover list does not contain multiple aliases for the same real node.
    urls.clear();
    std::vector<std::string> seenEffectiveHosts;
    for (const auto& p : probes) {
        std::string chosenUrl = p.url;
        if (p.ok && archiveEffectiveUrlUsable(p.effectiveUrl)) chosenUrl = p.effectiveUrl;
        const std::string chosenHost = archiveUrlHostLower(chosenUrl);
        bool duplicateHost = false;
        if (!chosenHost.empty()) {
            for (const auto& seen : seenEffectiveHosts) {
                if (seen == chosenHost) {
                    duplicateHost = true;
                    break;
                }
            }
        }
        if (duplicateHost) {
            archiveNodeDiagnostic(std::string("EFFECTIVE_DUPLICATE_SKIPPED host=") + chosenHost +
                " requested=" + p.url + " effective=" + (p.effectiveUrl.empty() ? "-" : p.effectiveUrl));
            continue;
        }
        if (!chosenHost.empty()) seenEffectiveHosts.push_back(chosenHost);
        urls.push_back(chosenUrl);
    }

    if (urls.empty()) {
        httpDiagnostic("archive selector: effective candidate list unexpectedly empty; keep normal archive.org path");
        return false;
    }

    char chosen[900];
    sceClibSnprintf(chosen, sizeof(chosen),
        "archive selector chose body_speed=%llu B/s total_speed=%llu B/s ttfb_ms=%llu total=%llu requested=%s effective=%s selected=%s",
        (unsigned long long)probes.front().bodyBytesPerSecond,
        (unsigned long long)probes.front().bytesPerSecond,
        (unsigned long long)(probes.front().ttfbUs / 1000ULL),
        (unsigned long long)probes.front().total,
        probes.front().url.c_str(),
        probes.front().effectiveUrl.empty() ? "-" : probes.front().effectiveUrl.c_str(),
        urls.front().c_str());
    httpDiagnostic(chosen);
    archiveNodeDiagnostic(std::string("SELECTED ") + chosen);
    return probes.front().ok;
''',
    'effective URL selection and dedup')

CPP.write_text(text, encoding='utf-8')

# Keep the network design documentation aligned with the runtime behavior.
doc = TLS.read_text(encoding='utf-8')
doc = replace_once(
    doc,
    '''When possible, the client prefers storage nodes that do not look like problematic `dn*` / `.ca` edges.
''',
    '''`dn*` / `.ca.archive.org` nodes are treated as **historically risky**, not forbidden. They remain lower-priority during discovery, but for large files they are probed exactly like `ia*` nodes. A risky edge that successfully handles TLS/HTTP Range and measures faster on the user's own Vita may be selected.
''',
    'TLS risky host policy')

doc = replace_once(
    doc,
    '''4. for files of **16 MiB or larger**, probe each direct candidate **sequentially** with a bounded **256 KiB** Range request;
5. rank successful candidates by measured bytes/second and start the real transfer on the fastest one;
6. keep the remaining ranked candidates as the existing failover order.
''',
    '''4. for files of **16 MiB or larger**, probe every direct candidate — including `dn*` / `.ca` risk candidates — **sequentially** with a bounded **256 KiB** Range request;
5. record both whole-request speed and **body throughput** (`bytes / (elapsed - TTFB)`), then rank successful candidates primarily by body throughput with lower TTFB as a tie-breaker;
6. follow Archive redirects during probes, rank the **effective** storage destination actually reached, and deduplicate candidates that resolve to the same effective host;
7. start the real transfer directly from the best usable effective URL and keep the remaining unique effective candidates as failover.
''',
    'TLS selector algorithm')

doc = replace_once(
    doc,
    '''`archive_nodes.log` is reset on every client launch and is intentionally verbose. It records the Archive identifier/file, catalog threshold hint, metadata `server`/`d1`/`d2`/`dir`, usable candidates, one-byte fallback probes, 256 KiB speed probes, HTTP/curl result, requested/effective URL, remote IP, redirects, TTFB, elapsed time, bytes, remote total, measured B/s + KiB/s + MiB/s, sorted ranking, selected node, every real transfer attempt, TLS verify result, failover reason/from/to, and the final effective node/outcome. Probe bytes are never written to the payload.
''',
    '''`archive_nodes.log` is reset on every client launch and is intentionally verbose. It records the Archive identifier/file, catalog threshold hint, metadata `server`/`d1`/`d2`/`dir`, normal versus `dn*`/`.ca` risk candidates, one-byte fallback probes, 256 KiB speed probes, HTTP/curl result, requested/effective URL, remote IP, redirects, TTFB, elapsed/body time, whole-request throughput, body throughput, effective-host deduplication, sorted ranking, selected effective node, every real transfer attempt, TLS verify result, failover reason/from/to, and the final effective node/outcome. Probe bytes are never written to the payload.
''',
    'TLS detailed log description')

TLS.write_text(doc, encoding='utf-8')
