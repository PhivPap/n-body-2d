#pragma once

#include <cstdint>
#include <string>


// Numbers as the panels and the hub show them
namespace Ui::Format {
std::string with_thousands(uint64_t n);  // "296,297"
// Three significant digits, with a k, M or G prefix from a thousand on: "358", "1.31 M"
std::string si(double x);
}  // namespace Ui::Format
