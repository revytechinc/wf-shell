#include "traffic-history.hpp"

#include "network-info.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace wf_net
{

std::string safe_traffic_graph_style(const std::string& s)
{
    for (int i = 0; TRAFFIC_GRAPH_STYLES[i]; ++i)
    {
        if (s == TRAFFIC_GRAPH_STYLES[i])
        {
            return s;
        }
    }
    return "wave-fill";
}

const char *traffic_graph_style_label(const std::string& s)
{
    const std::string id = safe_traffic_graph_style(s);
    for (int i = 0; TRAFFIC_GRAPH_STYLES[i]; ++i)
    {
        if (id == TRAFFIC_GRAPH_STYLES[i])
        {
            return TRAFFIC_GRAPH_STYLES[i];
        }
    }
    return "wave-fill";
}

bool is_valid_traffic_ifname(const std::string& name)
{
    if (name.empty() || name.size() > 15) /* IFNAMSIZ-1 */
    {
        return false;
    }
    /* Must start with letter (FreeBSD iface names) */
    if (!std::isalpha(static_cast<unsigned char>(name[0])))
    {
        return false;
    }
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.')
        {
            continue;
        }
        return false;
    }
    if (name.find("..") != std::string::npos)
    {
        return false;
    }
    return true;
}

void traffic_ring_push(std::deque<TrafficSample>& ring, size_t capacity,
    const TrafficSample& sample)
{
    if (capacity == 0)
    {
        return;
    }
    ring.push_back(sample);
    while (ring.size() > capacity)
    {
        ring.pop_front();
    }
}

void traffic_ring_push(std::vector<TrafficSample>& ring, size_t capacity,
    const TrafficSample& sample)
{
    if (capacity == 0)
    {
        return;
    }
    ring.push_back(sample);
    while (ring.size() > capacity)
    {
        ring.erase(ring.begin());
    }
}

uint64_t traffic_rate_Bps(uint64_t bytes_now, uint64_t bytes_prev, int64_t dt_ms)
{
    if (dt_ms <= 0)
    {
        return 0;
    }
    if (bytes_now < bytes_prev)
    {
        return 0;
    }
    const uint64_t delta = bytes_now - bytes_prev;
    return (delta * 1000ull + static_cast<uint64_t>(dt_ms) / 2) /
        static_cast<uint64_t>(dt_ms);
}

bool parse_netstat_if_bytes(const std::string& text,
    uint64_t *rx_bytes, uint64_t *tx_bytes,
    uint64_t *rx_pkts, uint64_t *tx_pkts)
{
    if (!rx_bytes || !tx_bytes)
    {
        return false;
    }
    *rx_bytes = 0;
    *tx_bytes = 0;
    if (rx_pkts)
    {
        *rx_pkts = 0;
    }
    if (tx_pkts)
    {
        *tx_pkts = 0;
    }
    if (text.empty())
    {
        return false;
    }

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.find("Name") != std::string::npos &&
            line.find("Ibytes") != std::string::npos)
        {
            continue;
        }
        if (line.find("<Link") == std::string::npos &&
            line.find("Link#") == std::string::npos)
        {
            continue;
        }
        std::istringstream ls(line);
        std::vector<std::string> tok;
        std::string t;
        while (ls >> t)
        {
            tok.push_back(t);
        }
        if (tok.size() < 11)
        {
            continue;
        }
        try
        {
            const uint64_t ipkts  = std::stoull(tok[4]);
            const uint64_t ibytes = std::stoull(tok[7]);
            const uint64_t opkts  = std::stoull(tok[8]);
            const uint64_t obytes = std::stoull(tok[10]);
            *rx_bytes = ibytes;
            *tx_bytes = obytes;
            if (rx_pkts)
            {
                *rx_pkts = ipkts;
            }
            if (tx_pkts)
            {
                *tx_pkts = opkts;
            }
            return true;
        }
        catch (...)
        {
            continue;
        }
    }
    return false;
}

/* ─── TrafficCollector ───────────────────────────────────────────────────── */

TrafficCollector::TrafficCollector() = default;

TrafficCollector::~TrafficCollector()
{
    stop();
}

void TrafficCollector::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
    {
        return; /* already running */
    }
    stop_ = false;
    thr_ = std::thread([this] () { thread_main(); });
}

void TrafficCollector::stop()
{
    if (!running_.load())
    {
        return;
    }
    stop_ = true;
    if (thr_.joinable())
    {
        thr_.join();
    }
    running_ = false;
}

bool TrafficCollector::is_running() const
{
    return running_.load();
}

void TrafficCollector::watch(const std::string& ifname)
{
    if (!is_valid_traffic_ifname(ifname))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    ifaces_.emplace(ifname, IfaceState{});
}

void TrafficCollector::unwatch(const std::string& ifname)
{
    std::lock_guard<std::mutex> lock(mu_);
    ifaces_.erase(ifname);
}

void TrafficCollector::sync_ifaces(const std::vector<std::string>& live_names)
{
    std::unordered_map<std::string, bool> want;
    want.reserve(live_names.size());
    for (const auto& n : live_names)
    {
        if (is_valid_traffic_ifname(n))
        {
            want[n] = true;
        }
    }

    std::lock_guard<std::mutex> lock(mu_);
    /* Remove gone */
    for (auto it = ifaces_.begin(); it != ifaces_.end(); )
    {
        if (want.find(it->first) == want.end())
        {
            it = ifaces_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    /* Add new */
    for (const auto& kv : want)
    {
        ifaces_.emplace(kv.first, IfaceState{});
    }
}

std::vector<std::string> TrafficCollector::watched_names() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string> out;
    out.reserve(ifaces_.size());
    for (const auto& kv : ifaces_)
    {
        out.push_back(kv.first);
    }
    return out;
}

size_t TrafficCollector::watched_count() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return ifaces_.size();
}

TrafficSeries TrafficCollector::snapshot(const std::string& ifname) const
{
    TrafficSeries out;
    out.ifname = ifname;
    std::lock_guard<std::mutex> lock(mu_);
    auto it = ifaces_.find(ifname);
    if (it == ifaces_.end())
    {
        return out;
    }
    const IfaceState& st = it->second;
    out.watched  = true;
    out.present  = st.present;
    out.rx_total = st.rx_bytes;
    out.tx_total = st.tx_bytes;
    out.rx_pkts  = st.rx_pkts;
    out.tx_pkts  = st.tx_pkts;
    out.samples.assign(st.ring.begin(), st.ring.end());
    if (!st.ring.empty())
    {
        out.last_rx_Bps = st.ring.back().rx_Bps;
        out.last_tx_Bps = st.ring.back().tx_Bps;
    }
    return out;
}

void TrafficCollector::apply_reading(const std::string& name, bool ok,
    uint64_t rx, uint64_t tx, uint64_t rxp, uint64_t txp, int64_t now_ms)
{
    /* Caller must hold mu_ */
    auto it = ifaces_.find(name);
    if (it == ifaces_.end())
    {
        return; /* unwatched while sampling — drop */
    }
    IfaceState& st = it->second;
    TrafficSample s;
    s.mono_ms = now_ms;
    s.valid   = ok;

    if (!ok)
    {
        st.miss_streak++;
        st.present = false;
        s.rx_Bps = 0;
        s.tx_Bps = 0;
        s.rx_bytes = st.rx_bytes;
        s.tx_bytes = st.tx_bytes;
        if (st.miss_streak >= k_traffic_miss_reset)
        {
            st.have_prev = false; /* re-baseline when it returns */
        }
        traffic_ring_push(st.ring, k_traffic_history_cap, s);
        return;
    }

    st.miss_streak = 0;
    st.present = true;
    s.rx_bytes = rx;
    s.tx_bytes = tx;
    if (st.have_prev)
    {
        s.rx_Bps = traffic_rate_Bps(rx, st.rx_bytes, now_ms - st.last_ms);
        s.tx_Bps = traffic_rate_Bps(tx, st.tx_bytes, now_ms - st.last_ms);
    }
    st.rx_bytes = rx;
    st.tx_bytes = tx;
    st.rx_pkts  = rxp;
    st.tx_pkts  = txp;
    st.last_ms  = now_ms;
    st.have_prev = true;
    traffic_ring_push(st.ring, k_traffic_history_cap, s);
}

void TrafficCollector::inject_for_test(const std::string& ifname,
    uint64_t rx_bytes, uint64_t tx_bytes, int64_t mono_ms, bool poll_ok)
{
    if (!is_valid_traffic_ifname(ifname))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    ifaces_.emplace(ifname, IfaceState{});
    apply_reading(ifname, poll_ok, rx_bytes, tx_bytes, 0, 0, mono_ms);
}

void TrafficCollector::sample_once_for_test(int64_t now_ms)
{
    sample_once(now_ms);
}

void TrafficCollector::sample_once(int64_t now_ms)
{
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mu_);
        names.reserve(ifaces_.size());
        for (const auto& kv : ifaces_)
        {
            names.push_back(kv.first);
        }
    }

    struct Reading
    {
        std::string name;
        uint64_t rx = 0, tx = 0, rxp = 0, txp = 0;
        bool ok = false;
    };
    std::vector<Reading> readings;
    readings.reserve(names.size());

    for (const auto& name : names)
    {
        Reading r;
        r.name = name;
        if (!is_valid_traffic_ifname(name))
        {
            readings.push_back(r);
            continue;
        }
        const std::string cmd = "netstat -I " + name + " -b -n 2>/dev/null";
        std::string out;
        try
        {
            if (info_hooks().run_cmd)
            {
                out = info_hooks().run_cmd(cmd);
            }
            else
            {
                FILE *fp = popen(cmd.c_str(), "r");
                if (fp)
                {
                    char buf[512];
                    while (fgets(buf, sizeof(buf), fp))
                    {
                        out += buf;
                    }
                    pclose(fp);
                }
            }
            r.ok = parse_netstat_if_bytes(out, &r.rx, &r.tx, &r.rxp, &r.txp);
        }
        catch (...)
        {
            r.ok = false;
        }
        readings.push_back(r);
    }

    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& r : readings)
    {
        apply_reading(r.name, r.ok, r.rx, r.tx, r.rxp, r.txp, now_ms);
    }
}

void TrafficCollector::thread_main()
{
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (!stop_.load())
    {
        const auto now = clock::now();
        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        try
        {
            sample_once(ms);
        }
        catch (...)
        {
            /* never kill the collector thread */
        }

        next += std::chrono::milliseconds(k_traffic_sample_ms);
        if (stop_.load())
        {
            break;
        }
        std::this_thread::sleep_until(next);
        if (clock::now() > next + std::chrono::milliseconds(k_traffic_sample_ms * 2))
        {
            next = clock::now();
        }
    }
}

} // namespace wf_net
