#include "mic_session_state.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", message);
    ++failures;
}

} // namespace

int main() {
    MicSessionStateTracker tracker;

    auto first = tracker.update(L"session-a", MicSessionActivity::Inactive);
    expect(first.inserted, "first session should be inserted");
    expect(first.stateChanged, "first insertion should report a state change");
    expect(first.activeCount == 0, "inactive insertion must not increment activity");
    expect(first.trackedCount == 1, "one session should be tracked");

    auto active = tracker.update(L"session-a", MicSessionActivity::Active);
    expect(!active.inserted, "activation should update the existing session");
    expect(active.stateChanged, "activation should change state");
    expect(active.activeSetChanged, "activation should change the active set");
    expect(active.activeCount == 1, "one session should be active");

    auto duplicateActive = tracker.update(L"session-a", MicSessionActivity::Active);
    expect(!duplicateActive.stateChanged, "duplicate Active must be idempotent");
    expect(!duplicateActive.activeSetChanged, "duplicate Active must not change counts");
    expect(duplicateActive.activeCount == 1, "duplicate Active must not double count");

    tracker.update(L"session-b", MicSessionActivity::Active);
    expect(tracker.activeCount() == 2, "two independent sessions should be active");

    tracker.update(L"session-a", MicSessionActivity::Inactive);
    expect(tracker.activeCount() == 1, "one inactive session should leave one active");

    // A rapid restart of the same session must reactivate exactly once.
    tracker.update(L"session-a", MicSessionActivity::Active);
    tracker.update(L"session-a", MicSessionActivity::Active);
    expect(tracker.activeCount() == 2, "rapid restart must reactivate exactly once");

    // Expiration without a final Inactive callback must still remove activity.
    tracker.update(L"session-a", MicSessionActivity::Expired);
    expect(tracker.activeCount() == 1, "expiration must remove active membership");
    expect(tracker.remove(L"session-a"), "expired session should be removable");
    expect(!tracker.contains(L"session-a"), "removed session must not remain tracked");

    tracker.update(L"session-b", MicSessionActivity::Inactive);
    expect(tracker.activeCount() == 0, "all sessions should now be inactive");
    expect(tracker.remove(L"session-b"), "second session should be removable");
    expect(tracker.trackedCount() == 0, "tracker should be empty");

    if (failures != 0) {
        std::fprintf(stderr, "mic_session_state_test failed: %d checks\n", failures);
        return 1;
    }

    std::puts("mic_session_state_test passed");
    return 0;
}
