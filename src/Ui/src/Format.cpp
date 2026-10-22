#include "Ui/Format.hpp"

#include <cmath>
#include <iterator>

#include "fmt/format.h"


namespace Ui::Format {
std::string with_thousands(uint64_t n) {
    std::string digits = std::to_string(n);
    for (size_t k = digits.size(); k > 3; k -= 3) {
        digits.insert(k - 3, ",");
    }
    return digits;
}

std::string si(double x) {
    if (!std::isfinite(x)) {
        return "-";
    }
    constexpr const char* PREFIXES[] = {"", " k", " M", " G"};
    size_t prefix = 0;
    while (std::abs(x) >= 999.5 && prefix + 1 < std::size(PREFIXES)) {  // not "1e+03"
        x /= 1000.0;
        prefix++;
    }
    return fmt::format("{:.3g}{}", x, PREFIXES[prefix]);
}
}  // namespace Ui::Format
