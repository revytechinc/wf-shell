#include "traffic-history.hpp"

#include "network-info.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <vector>

namespace wf_net
{

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
        /* Counter wrap / iface reset */
        return 0;
    }
    const uint64_t delta = bytes_now - bytes_prev;
    /* round: (delta * 1000 + dt/2) / dt */
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

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        /* Skip header */
        if (line.find("Name") != std::string::npos &&
            line.find("Ibytes") != std::string::npos)
        {
            continue;
        }
        /* Link row contains "<Link" or "Link#" */
        if (line.find("<Link") == std::string::npos &&
            line.find("Link#") == std::string::npos)
        {
            continue;
        }
        /*
         * Columns (FreeBSD netstat -I IF -b -n):
         * Name Mtu Network Address Ipkts Ierrs Idrop Ibytes Opkts Oerrs Obytes Coll
         * We scan for the large integer fields by token position after Address.
         */
        std::istringstream ls(line);
        std::vector<std::string> tok;
        std::string t;
        while (ls >> t)
        {
            tok.push_back(t);
        }
        /* Need at least: Name Mtu Net Addr Ipkts Ierrs Idrop Ibytes Opkts Oerrs Obytes */
        if (tok.size() < 11)
        {
            continue;
        }
        /* Heuristic: Ibytes is 8th field (index 7), Obytes index 10 on Link line */
        try
        {
            const uint64_t ipkts = std::stoull(tok[4]);
            const uint64_t ibytes = std::stoull(tok[7]);
            const uint64_t opkts = std::stoull(tok[8]);
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
    std::lock_guard<std::mutex> lock(mu_);
    if (running_)
    {
        return;
    }
    stop_ = false;
    running_ = true;
    thr_ = std::thread([this] () { thread_main(); });
}

void TrafficCollector::stop()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!running_)
        {
            return;
        }
        stop_ = true;
    }
    if (thr_.joinable())
    {
        thr_.join();
    }
    std::lock_guard<std::mutex> lock(mu_);
    running_ = false;
}

void TrafficCollector::watch(const std::string& ifname)
{
    if (ifname.empty())
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
    out.rx_total = st.rx_bytes;
    out.tx_total = st.tx_bytes;
    out.rx_pkts  = st.rx_pkts;
    out.tx_pkts  = st.tx_pkts;
    out.samples  = st.ring; /* copy */
    if (!st.ring.empty())
    {
        out.last_rx_Bps = st.ring.back().rx_Bps;
        out.last_tx_Bps = st.ring.back().tx_Bps;
    }
    return out;
}

void TrafficCollector::inject_for_test(const std::string& ifname,
    uint64_t rx_bytes, uint64_t tx_bytes, int64_t mono_ms)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto& st = ifaces_[ifname];
    TrafficSample s;
    s.mono_ms = mono_ms;
    s.rx_bytes = rx_bytes;
    s.tx_bytes = tx_bytes;
    if (st.have_prev)
    {
        s.rx_Bps = traffic_rate_Bps(rx_bytes, st.rx_bytes, mono_ms - st.last_ms);
        s.tx_Bps = traffic_rate_Bps(tx_bytes, st.tx_bytes, mono_ms - st.last_ms);
    }
    st.rx_bytes = rx_bytes;
    st.tx_bytes = tx_bytes;
    st.last_ms = mono_ms;
    st.have_prev = true;
    traffic_ring_push(st.ring, k_traffic_history_cap, s);
}

void TrafficCollector::sample_once(int64_t now_ms)
{
    /* Copy names under lock, I/O outside lock (non-blocking for snapshot). */
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
        /* Fixed command shape — no shell metacharacters from ifname if we validate */
        bool safe = true;
        for (char c : name)
        {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                    c == '_' || c == '.'))
            {
                safe = false;
                break;
            }
        }
        if (!safe)
        {
            readings.push_back(r);
            continue;
        }
        std::string cmd = "netstat -I " + name + " -b -n 2>/dev/null";
        std::string out;
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
        readings.push_back(r);
    }

    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& r : readings)
    {
        auto it = ifaces_.find(r.name);
        if (it == ifaces_.end() || !r.ok)
        {
            continue;
        }
        IfaceState& st = it->second;
        TrafficSample s;
        s.mono_ms = now_ms;
        s.rx_bytes = r.rx;
        s.tx_bytes = r.tx;
        if (st.have_prev)
        {
            s.rx_Bps = traffic_rate_Bps(r.rx, st.rx_bytes, now_ms - st.last_ms);
            s.tx_Bps = traffic_rate_Bps(r.tx, st.tx_bytes, now_ms - st.last_ms);
        }
        st.rx_bytes = r.rx;
        st.tx_bytes = r.tx;
        st.rx_pkts  = r.rxp;
        st.tx_pkts  = r.txp;
        st.last_ms  = now_ms;
        st.have_prev = true;
        traffic_ring_push(st.ring, k_traffic_history_cap, s);
    }
}

void TrafficCollector::thread_main()
{
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_)
            {
                break;
            }
        }
        const auto now = clock::now();
        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        sample_once(ms);

        next += std::chrono::milliseconds(k_traffic_sample_ms);
        std::this_thread::sleep_until(next);
        /* If we fell behind, resync to avoid burst */
        if (clock::now() > next + std::chrono::milliseconds(k_traffic_sample_ms))
        {
            next = clock::now();
        }
    }
}

} // namespace wf_net
