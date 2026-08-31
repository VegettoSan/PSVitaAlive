bool categoryWantsDataFiles(const std::string& category) {
    std::string c = lowerAscii(category);
    // ports / games / emulators (+ media). Skip utilities & plugins.
    return c == "ports" || c == "games" || c == "emulators" || c == "media"
        || c.find("port") != std::string::npos
        || c.find("game") != std::string::npos
        || c.find("emulator") != std::string::npos;
}
