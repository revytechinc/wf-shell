#pragma once

/**
 * Per-interface RX/TX history for Details histogram.
 * - Window: last 5 minutes only (ring buffer).
 * - Collection: optional background thread (non-blocking snapshots for UI).
 */

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace wf_net
{

/** Keep five minutes at 1 sample / second. */
inline constexpr int k_traffic_history_sec   = 300;
inline constexpr int k_traffic_sample_ms     = 1000;
inline constexpr size_t k_traffic_history_cap =
    static_cast<size_t>(k_traffic_history_sec);

struct TrafficSample
{
    int64_t  mono_ms  = 0;   /**< collector clock (ms) */
    uint64_t rx_Bps   = 0;   /**< receive bytes/sec over last interval */
    uint64_t tx_Bps   = 0;
    uint64_t rx_bytes = 0;   /**< cumulative counters at sample time */
    uint64_t tx_bytes = 0;
};

/** UI-safe copy of one interface series (oldest → newest). */
struct TrafficSeries
{
    std::string ifname;
    uint64_t rx_total = 0;
    uint64_t tx_total = 0;
    uint64_t rx_pkts  = 0;
    uint64_t tx_pkts  = 0;
    uint64_t last_rx_Bps = 0;
    uint64_t last_tx_Bps = 0;
    std::vector<TrafficSample> samples;
};

/**
 * Append sample; drop oldest when size > capacity.
 * Pure (no I/O). capacity should be k_traffic_history_cap.
 */
void traffic_ring_push(std::vector<TrafficSample>& ring, size_t capacity,
    const TrafficSample& sample);

/**
 * Rate in bytes/sec from counter delta. 0 if dt_ms <= 0 or counter reset.
 * Pure.
 */
uint64_t traffic_rate_Bps(uint64_t bytes_now, uint64_t bytes_prev, int64_t dt_ms);

/**
 * Parse FreeBSD `netstat -I IF -b -n` Link# line for Ibytes/Obytes/Ipkts/Opkts.
 * Pure. Returns false if not found.
 */
bool parse_netstat_if_bytes(const std::string& text,
    uint64_t *rx_bytes, uint64_t *tx_bytes,
    uint64_t *rx_pkts = nullptr, uint64_t *tx_pkts = nullptr);

/**
 * Background sampler: one thread, all watched interfaces, 1 Hz.
 * UI must only call snapshot() (mutex, short critical section — never blocks on I/O).
 */
class TrafficCollector
{
  public:
    TrafficCollector();
    ~TrafficCollector();

    TrafficCollector(const TrafficCollector&) = delete;
    TrafficCollector& operator=(const TrafficCollector&) = delete;

    void start();
    void stop();

    /** Add interface to the poll set (idempotent). */
    void watch(const std::string& ifname);
    void unwatch(const std::string& ifname);

    /** Non-blocking copy of last ≤5 min of rates for ifname. */
    TrafficSeries snapshot(const std::string& ifname) const;

    /** Test hook: inject one sample without sleeping (must hold no UI lock). */
    void inject_for_test(const std::string& ifname, uint64_t rx_bytes,
        uint64_t tx_bytes, int64_t mono_ms);

  private:
    struct IfaceState
    {
        uint64_t rx_bytes = 0;
        uint64_t tx_bytes = 0;
        uint64_t rx_pkts  = 0;
        uint64_t tx_pkts  = 0;
        int64_t  last_ms  = 0;
        bool     have_prev = false;
        std::vector<TrafficSample> ring;
    };

    void thread_main();
    void sample_once(int64_t now_ms);

    mutable std::mutex mu_;
    std::unordered_map<std::string, IfaceState> ifaces_;
    std::thread thr_;
    bool running_ = false;
    bool stop_    = false;
};

} // namespace wf_net
