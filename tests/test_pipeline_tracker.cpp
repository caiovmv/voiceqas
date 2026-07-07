#include <gtest/gtest.h>

#include "voiceqas/ops/pipeline_tracker.hpp"

namespace voiceqas::ops {

TEST(PipelineTrackerTest, BuildSnapshotContainsStages) {
    auto& tracker = PipelineTracker::instance();
    tracker.record_rtp_ingress("snap-test", 320, 1.0, 0.5);
    const auto snapshot = tracker.build_snapshot("snap-test");
    EXPECT_EQ(snapshot["scope"], "session");
    EXPECT_TRUE(snapshot.contains("stages"));
    tracker.remove_session("snap-test");
}

}  // namespace voiceqas::ops
