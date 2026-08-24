#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

enum class MicSessionActivity {
    Inactive,
    Active,
    Expired
};

class MicSessionStateTracker {
public:
    struct UpdateResult {
        bool inserted{false};
        bool stateChanged{false};
        bool activeSetChanged{false};
        size_t activeCount{0};
        size_t trackedCount{0};
    };

    UpdateResult update(const std::wstring& key, MicSessionActivity state) {
        auto [it, inserted] = m_states.emplace(key, state);
        bool stateChanged = inserted;
        bool activeSetChanged = false;

        if (inserted) {
            if (state == MicSessionActivity::Active) {
                ++m_activeCount;
                activeSetChanged = true;
            }
        } else if (it->second != state) {
            const bool wasActive = it->second == MicSessionActivity::Active;
            const bool isActive = state == MicSessionActivity::Active;
            it->second = state;
            stateChanged = true;
            if (wasActive != isActive) {
                activeSetChanged = true;
                if (isActive) {
                    ++m_activeCount;
                } else if (m_activeCount > 0) {
                    --m_activeCount;
                }
            }
        }

        return {inserted, stateChanged, activeSetChanged,
            m_activeCount, m_states.size()};
    }

    bool remove(const std::wstring& key) {
        auto it = m_states.find(key);
        if (it == m_states.end()) return false;
        if (it->second == MicSessionActivity::Active && m_activeCount > 0) {
            --m_activeCount;
        }
        m_states.erase(it);
        return true;
    }

    bool contains(const std::wstring& key) const {
        return m_states.find(key) != m_states.end();
    }

    size_t activeCount() const { return m_activeCount; }
    size_t trackedCount() const { return m_states.size(); }

    void clear() {
        m_states.clear();
        m_activeCount = 0;
    }

private:
    std::unordered_map<std::wstring, MicSessionActivity> m_states;
    size_t m_activeCount{0};
};
