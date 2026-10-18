#pragma once
#include <string>
namespace psvitaalive { namespace ui {
// Save (or reuse) a tiny one-module-per-pixel QR PNG under Image Cache v3 buckets.
// Uses no network and requires only the URL. White 4-module quiet zone included.
bool ensureWebQrPng(const std::string& url, std::string& path, std::string& error);
}} // namespace psvitaalive::ui
