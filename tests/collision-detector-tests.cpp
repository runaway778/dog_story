#define _USE_MATH_DEFINES

#include <cmath>
#include <functional>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_templated.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/collision_detector.h"

namespace Catch {
template<>
struct StringMaker<collision_detector::GatheringEvent> {
    static std::string convert(collision_detector::GatheringEvent const& value) {
        std::ostringstream tmp;
        tmp << "(" << value.gatherer_id << value.item_id << value.sq_distance << value.time << ")";

        return tmp.str();
    }
};
}  // namespace Catch

namespace {

template <typename Range, typename Predicate>
struct EqualsRangeMatcher : Catch::Matchers::MatcherGenericBase {
    EqualsRangeMatcher(Range const& range, Predicate predicate)
        : range_{range}
        , predicate_{predicate} {
    }

    template <typename OtherRange>
    bool match(const OtherRange& other) const {
        using std::begin;
        using std::end;

        return std::equal(begin(range_), end(range_), begin(other), end(other), predicate_);
    }

    std::string describe() const override {
        return "Equals: " + Catch::rangeToString(range_);
    }

private:
    const Range& range_;
    Predicate predicate_;
};

template <typename Range, typename Predicate>
auto EqualsRange(const Range& range, Predicate prediate) {
    return EqualsRangeMatcher<Range, Predicate>{range, prediate};
}

class TestVectorItemGathererProvider : public collision_detector::ItemGathererProvider {
public:
    TestVectorItemGathererProvider(std::vector<collision_detector::Item> items,
                               std::vector<collision_detector::Gatherer> gatherers)
        : items_(items)
        , gatherers_(gatherers) {
    }

    
    size_t ItemsCount() const override {
        return items_.size();
    }
    collision_detector::Item GetItem(size_t idx) const override {
        return items_.at(idx);
    }
    size_t GatherersCount() const override {
        return gatherers_.size();
    }
    collision_detector::Gatherer GetGatherer(size_t idx) const override {
        return gatherers_.at(idx);
    }

private:
    std::vector<collision_detector::Item> items_;
    std::vector<collision_detector::Gatherer> gatherers_;
};

class CompareEvents {
public:
    bool operator()(const collision_detector::GatheringEvent& l,
                    const collision_detector::GatheringEvent& r) {
        if (l.gatherer_id != r.gatherer_id || l.item_id != r.item_id) 
            return false;

        static const double eps = 1e-10;

        if (std::abs(l.sq_distance - r.sq_distance) > eps) {
            return false;
        }

        if (std::abs(l.time - r.time) > eps) {
            return false;
        }
        return true;
    }
};

}

SCENARIO("Collision detection") {
    WHEN("no items") {
        TestVectorItemGathererProvider provider{
            {}, {{{1, 2}, {4, 2}, 5.}, {{0, 0}, {10, 10}, 5.}, {{-5, 0}, {10, 5}, 5.}}};
        THEN("No events") {
            auto events = collision_detector::FindGatherEvents(provider);
            CHECK(events.empty());
        }
    }
    WHEN("no gatherers") {
        TestVectorItemGathererProvider provider{
            {{{1, 2}, 5.}, {{0, 0}, 5.}, {{-5, 0}, 5.}}, {}};
        THEN("No events") {
            auto events = collision_detector::FindGatherEvents(provider);
            CHECK(events.empty());
        }
    }
    WHEN("multiple items on a way of gatherer") {
        TestVectorItemGathererProvider provider{{
            {{9, 0.27}, .1},
            {{8, 0.24}, .1},
            {{7, 0.21}, .1},
            {{6, 0.18}, .1},
            {{5, 0.15}, .1},
            {{4, 0.12}, .1},
            {{3, 0.09}, .1},
            {{2, 0.06}, .1},
            {{1, 0.03}, .1},
            {{0, 0.0}, .1},
            {{-1, 0}, .1},
            }, {
            {{0, 0}, {10, 0}, 0.1},
        }};
        THEN("Gathered items in right order") {
            auto events = collision_detector::FindGatherEvents(provider);
            CHECK_THAT(
                events,
                EqualsRange(std::vector{
                    collision_detector::GatheringEvent{9, 0,0.*0., 0.0},
                    collision_detector::GatheringEvent{8, 0,0.03*0.03, 0.1},
                    collision_detector::GatheringEvent{7, 0,0.06*0.06, 0.2},
                    collision_detector::GatheringEvent{6, 0,0.09*0.09, 0.3},
                    collision_detector::GatheringEvent{5, 0,0.12*0.12, 0.4},
                    collision_detector::GatheringEvent{4, 0,0.15*0.15, 0.5},
                    collision_detector::GatheringEvent{3, 0,0.18*0.18, 0.6},
                }, CompareEvents()));
        }
    }
    WHEN("multiple gatherers and one item") {
        TestVectorItemGathererProvider provider{{
                                                {{0, 0}, 0.},
                                            },
                                            {
                                                {{-5, 0}, {5, 0}, 1.},
                                                {{0, 1}, {0, -1}, 1.},
                                                {{-10, 10}, {101, -100}, 0.5}, // <-- that one
                                                {{-100, 100}, {10, -10}, 0.5},
                                            }
        };
        THEN("Item gathered by faster gatherer") {
            auto events = collision_detector::FindGatherEvents(provider);
            CHECK(events.front().gatherer_id == 2);
        }
    }
    WHEN("Gatherers stay put") {
        TestVectorItemGathererProvider provider{{
                                                {{0, 0}, 10.},
                                            },
                                            {
                                                {{-5, 0}, {-5, 0}, 1.},
                                                {{0, 0}, {0, 0}, 1.},
                                                {{-10, 10}, {-10, 10}, 100}
                                            }
        };
        THEN("No events detected") {
            auto events = collision_detector::FindGatherEvents(provider);

            CHECK(events.empty());
        }
    }
}

using namespace collision_detector;
using Catch::Matchers::WithinAbs;

TEST_CASE("One gatherer moving right, one item exactly on its way") {
    Item item{{0, 0}, 1};
    Gatherer gatherer{{-1, 0}, {1, 0}, 0.6};
    TestVectorItemGathererProvider provider{{item}, {gatherer}};
    std::vector<GatheringEvent> events = FindGatherEvents(provider);
    CHECK(events.size() == 1);
    CHECK_THAT(events[0].sq_distance, WithinAbs(0.0, 1e-10));
    CHECK_THAT(events[0].time, WithinAbs(0.5, 1e-10));
}

TEST_CASE("One gatherer moving up, two items exactly on its way") {
    Item item1{{2, 3}, 1};
    Item item2{{2, -5}, 1};
    Gatherer gatherer{{2, -10}, {2, 15}, 0.6};
    TestVectorItemGathererProvider provider{{item1, item2}, {gatherer}};
    std::vector<GatheringEvent> events = FindGatherEvents(provider);
    CHECK(events.size() == 2);
    CHECK_THAT(events[0].sq_distance, WithinAbs(0.0, 1e-10));
    CHECK_THAT(events[0].time, WithinAbs(0.2, 1e-10));
    CHECK_THAT(events[1].sq_distance, WithinAbs(0.0, 1e-10));
    CHECK_THAT(events[1].time, WithinAbs(0.52, 1e-10));
}

TEST_CASE("One gatherer moving left, two items near its way with different radiuses, two items not on its way, one is projecting on it, other doesn't") {
    Item item_missing1{{2, 3}, 1};
    Item item_missing2{{-5, -5}, 1};
    Item item1{{-10, 6}, 2.5};
    Item item2{{-5, 3}, 0.5};
    Gatherer gatherer{{-2.5, 4}, {-12.5, 4}, 0.5};
    TestVectorItemGathererProvider provider{{item_missing1, item_missing2, item1, item2}, {gatherer}};
    std::vector<GatheringEvent> events = FindGatherEvents(provider);
    CHECK(events.size() == 2);
    CHECK_THAT(events[0].sq_distance, WithinAbs(1.0, 1e-10));
    CHECK_THAT(events[0].time, WithinAbs(0.25, 1e-10));
    CHECK_THAT(events[1].sq_distance, WithinAbs(4.0, 1e-10));
    CHECK_THAT(events[1].time, WithinAbs(0.75, 1e-10));
}

TEST_CASE("Two gatherers moving diagonally and perpendicular, item achievable y both of them") {
    Item item{{0, 0}, 1};
    Gatherer gatherer1{{3, -4}, {-5, 4}, 1};
    Gatherer gatherer2{{5, 5}, {-5, -5}, 1};
    TestVectorItemGathererProvider provider{{item}, {gatherer1, gatherer2}};
    std::vector<GatheringEvent> events = FindGatherEvents(provider);
    CHECK(events.size() == 2);
    CHECK_THAT(events[0].sq_distance, WithinAbs(0.5, 1e-10));
    CHECK_THAT(events[0].time, WithinAbs(0.4375, 1e-10));
    CHECK_THAT(events[1].sq_distance, WithinAbs(0.0, 1e-10));
    CHECK_THAT(events[1].time, WithinAbs(0.5, 1e-10));
}