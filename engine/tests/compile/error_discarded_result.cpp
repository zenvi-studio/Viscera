#include <viscera/error/error.hpp>

namespace
{
    [[nodiscard]] viscera::Result<int> answer()
    {
        return 1;
    }
} // namespace

int main()
{
    answer();
    return 0;
}
