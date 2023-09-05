#include "loot_generator.h"

#include <algorithm>
#include <cmath>

namespace loot_gen {

uint64_t LootGenerator::Generate(TimeInterval time_delta, uint64_t loot_count,
                                 uint64_t looter_count) {
    time_without_loot_ += time_delta;
    const uint64_t loot_shortage = loot_count > looter_count ? 0u : looter_count - loot_count;
    const double ratio = std::chrono::duration<double>{time_without_loot_} / base_interval_;
    const double probability
        = std::clamp((1.0 - std::pow(1.0 - probability_, ratio)) * random_generator_(), 0.0, 1.0);
    const uint64_t generated_loot = static_cast<uint64_t>(std::round(loot_shortage * probability));
    if (generated_loot > 0) {
        time_without_loot_ = {};
    }
    return generated_loot;
}

} // namespace loot_gen
