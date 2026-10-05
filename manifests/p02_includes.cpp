// Part 2.1 -- a file that needs the standard library, to show what the
// platform flags are for.
#include <cmath>
#include <optional>
#include <string>

int length_or_zero(const std::optional<std::string> &s) {
  if (!s)
    return 0;
  return static_cast<int>(std::sqrt(static_cast<double>(s->size())));
}
