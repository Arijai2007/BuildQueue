#pragma once
namespace bq {
class SimulationClock {
    int tick_ = 0;

  public:
    int now() const {
        return tick_;
    }
    void advance() {
        ++tick_;
    }
    void reset() {
        tick_ = 0;
    }
};
} // namespace bq
