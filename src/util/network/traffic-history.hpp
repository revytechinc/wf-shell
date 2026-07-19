#pragma once

/**
 * Per-interface RX/TX history for Details histogram.
 *
 * - Window: last 5 minutes only (fixed-capacity ring @ 1 Hz).
 * - Ifaces: added/removed at any time via watch / unwatch / sync_ifaces.
 * - Collection: background thread; I/O never under the UI snapshot lock.
 * - Fail-soft: vanished iface, bad name, parse error, counter reset → no crash.
 */

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace wf_net
{

/** Keep five minutes at 1 sample / second. */
inline constexpr int k_traffic_history_sec = 300;
inline constexpr int k_traffic_sample_ms   = 1000;
inline constexpr size_t k_traffic_history_cap =
    static_cast<size_t>(k_traffic_history_sec);

/**
 * Traffic graph styles — line-oriented only (no bar charts).
 * Null-terminated list of stable ids for UI combos + ini.
 */
inline constexpr const char *TRAFFIC_GRAPH_STYLES[] = {
    "wave", "wave-fill", "mirror", "scope", "dots", "ribbon",
    nullptr
};

/** Return @s if known, else "wave-fill". Pure. */
std::string safe_traffic_graph_style(const std::string& s);

/** Human label for a style id (empty if unknown). Pure. */
const char *traffic_graph_style_label(const std::string& s);

/** After this many consecutive failed polls, re-baseline rates (iface gone/back). */
inline constexpr int k_traffic_miss_reset = 3;

struct TrafficSample
{
    int64_t  mono_ms  = 0;
    uint64_t rx_Bps   = 0;
    uint64_t tx_Bps   = 0;
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
    bool     valid    = true; /**< false = poll failed / iface missing this tick */
};

/** UI-safe copy of one interface series (oldest → newest). */
struct TrafficSeries
{
    std::string ifname;
    bool     watched     = false;
    bool     present     = false; /**< last poll succeeded */
    uint64_t rx_total    = 0;
    uint64_t tx_total    = 0;
    uint64_t rx_pkts     = 0;
    uint64_t tx_pkts     = 0;
    uint64_t last_rx_Bps = 0;
    uint64_t last_tx_Bps = 0;
    std::vector<TrafficSample> samples;
};

/**
 * True if name is safe for `netstat -I NAME` (no shell metacharacters).
 * Allows FreeBSD names: aq0, tap0, epair0a, vm-public, lo1, …
 * Pure.
 */
bool is_valid_traffic_ifname(const std::string& name);

/**
 * Append sample; drop oldest when size > capacity.
 * Pure. capacity 0 → no-op.
 */
void traffic_ring_push(std::deque<TrafficSample>& ring, size_t capacity,
    const TrafficSample& sample);

/** Same for vector (tests / simple callers). Pure. */
void traffic_ring_push(std::vector<TrafficSample>& ring, size_t capacity,
    const TrafficSample& sample);

/**
 * Rate in bytes/sec from counter delta.
 * 0 if dt_ms <= 0 or counter went backwards (reset/wrap).
 * Pure.
 */
uint64_t traffic_rate_Bps(uint64_t bytes_now, uint64_t bytes_prev, int64_t dt_ms);

/**
 * Parse FreeBSD `netstat -I IF -b -n` Link# line.
 * Pure. false on empty/garbage/missing Link row.
 */
bool parse_netstat_if_bytes(const std::string& text,
    uint64_t *rx_bytes, uint64_t *tx_bytes,
    uint64_t *rx_pkts = nullptr, uint64_t *tx_pkts = nullptr);

/**
 * Background sampler: one thread, dynamic iface set, 1 Hz.
 *
 * Thread-safety:
 * - watch / unwatch / sync_ifaces / snapshot / stop are safe from any thread.
 * - sample I/O runs outside the map lock so snapshot() never waits on netstat.
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
    bool is_running() const;

    /** Add interface (idempotent). Invalid names ignored. */
    void watch(const std::string& ifname);

    /** Remove interface and drop its history. */
    void unwatch(const std::string& ifname);

    /**
     * Reconcile watched set with current live names (probe result).
     * Adds new, removes gone — safe while the collector thread runs.
     */
    void sync_ifaces(const std::vector<std::string>& live_names);

    /** Names currently watched (copy). */
    std::vector<std::string> watched_names() const;

    size_t watched_count() const;

    /**
     * Non-blocking copy of ≤5 min history.
     * Empty samples if never watched / just added.
     */
    TrafficSeries snapshot(const std::string& ifname) const;

    /**
     * Test: apply counter reading as the worker would (no sleep, no thread).
     * Creates watch entry if missing and name is valid.
     */
    void inject_for_test(const std::string& ifname, uint64_t rx_bytes,
        uint64_t tx_bytes, int64_t mono_ms, bool poll_ok = true);

    /** Test: run one sample cycle (uses InfoHooks / netstat). */
    void sample_once_for_test(int64_t now_ms);

  private:
    struct IfaceState
    {
        uint64_t rx_bytes = 0;
        uint64_t tx_bytes = 0;
        uint64_t rx_pkts  = 0;
        uint64_t tx_pkts  = 0;
        int64_t  last_ms  = 0;
        bool     have_prev = false;
        bool     present   = false;
        int      miss_streak = 0;
        std::deque<TrafficSample> ring;
    };

    void thread_main();
    void sample_once(int64_t now_ms);
    void apply_reading(const std::string& name, bool ok,
        uint64_t rx, uint64_t tx, uint64_t rxp, uint64_t txp, int64_t now_ms);

    mutable std::mutex mu_;
    std::unordered_map<std::string, IfaceState> ifaces_;
    std::thread thr_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
};

} // namespace wf_net
