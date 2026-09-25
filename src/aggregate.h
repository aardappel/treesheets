#include <algorithm>
#include <numeric>
#include <vector>

// Aggregations over the numbers of a grid, shared by the evaluator's grid operations
// (Grid::Aggregate) and the ts.grid_* script functions. All return 0 for no numbers.
namespace aggregate {

inline double Sum(const std::vector<double> &v) { return std::accumulate(v.begin(), v.end(), 0.0); }

inline double Min(const std::vector<double> &v) {
    return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end());
}

inline double Max(const std::vector<double> &v) {
    return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
}

inline double Avg(const std::vector<double> &v) { return v.empty() ? 0.0 : Sum(v) / v.size(); }

// The middle number when sorted, or the average of the two middle ones if their count is even.
inline double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    auto mid = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), mid, v.end());
    if (v.size() % 2) return *mid;
    return (*mid + *std::max_element(v.begin(), mid)) / 2;
}

}  // namespace aggregate
