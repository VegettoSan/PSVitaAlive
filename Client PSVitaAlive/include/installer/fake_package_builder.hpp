#pragma once

#include <string>

namespace psvitaalive {

class FakePackageBuilder {
public:
    bool build(const std::string& packageDir);

    const std::string& lastError() const { return lastError_; }

private:
    std::string lastError_;

    void setError(const std::string& message);
};

} // namespace psvitaalive
