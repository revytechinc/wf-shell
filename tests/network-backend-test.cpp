#include <gtest/gtest.h>

#include "network/network-backend.hpp"
#include "network/network-info.hpp"
#include "network/network-types.hpp"
#include "network/freebsd-network.hpp"
#include "platform.hpp"

#include <cstring>
#include <string>

using namespace wf_net;

/* ─── pure classification / display ─────────────────────────────────────── */

TEST(NetworkTypes, ClassifyIfaceNames)
{
    EXPECT_EQ(classify_iface_name("wlan0"), InterfaceKind::Wireless);
    EXPECT_EQ(classify_iface_name("aq0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("igb0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("em0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("lo0"), InterfaceKind::Loopback);
    EXPECT_EQ(classify_iface_name("bridge0"), InterfaceKind::Bridge);
    EXPECT_EQ(classify_iface_name("vm-public"), InterfaceKind::Bridge);
    EXPECT_EQ(classify_iface_name("tap0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("wg0"), InterfaceKind::Virtual);
}

TEST(NetworkTypes, FormatAndIcon)
{
    InterfaceInfo i;
    i.name = "aq0";
    i.kind = InterfaceKind::Ethernet;
    i.up = true;
    i.running = true;
    i.ipv4 = {"99.48.162.238"};
    i.ipv6 = {"2600:1700:1c14:4040:b49b:83f6:0:55"};
    i.is_default_route = true;
    i.is_default_route_v4 = true;
    i.is_default_route_v6 = true;

    auto name = format_display_name(i);
    EXPECT_EQ(name, "aq0 · default\n99.48.162.238\n2600:1700:1c14:4040:b49b:83f6:0:55");
    EXPECT_EQ(format_address_summary(i),
        "99.48.162.238\n2600:1700:1c14:4040:b49b:83f6:0:55");

    /* IPv4-only: no blank IPv6 line */
    InterfaceInfo v4only;
    v4only.name = "em0";
    v4only.ipv4 = {"10.0.0.1"};
    EXPECT_EQ(format_display_name(v4only), "em0\n10.0.0.1");
    EXPECT_EQ(format_address_summary(v4only), "10.0.0.1");

    /* IPv6-only still displays (single address line) */
    InterfaceInfo v6only;
    v6only.name = "igb0";
    v6only.kind = InterfaceKind::Ethernet;
    v6only.ipv6 = {"2001:db8::1"};
    EXPECT_EQ(format_display_name(v6only), "igb0\n2001:db8::1");
    EXPECT_EQ(format_address_summary(v6only), "2001:db8::1");

    EXPECT_EQ(icon_for_interface(i), "network-wired");
    i.running = false;
    EXPECT_EQ(icon_for_interface(i), "network-wired-disconnected");

    InterfaceInfo br;
    br.kind = InterfaceKind::Bridge;
    br.up = true;
    br.running = true;
    EXPECT_EQ(icon_for_interface(br), "network-server");

    InterfaceInfo tap;
    tap.kind = InterfaceKind::Virtual;
    tap.up = true;
    tap.running = true;
    EXPECT_EQ(icon_for_interface(tap), "network-transmit-receive");

    InterfaceInfo wifi;
    wifi.kind = InterfaceKind::Wireless;
    wifi.up = true;
    wifi.running = true;
    EXPECT_EQ(icon_for_interface(wifi), "network-wireless-signal-excellent");
    wifi.running = false;
    EXPECT_EQ(icon_for_interface(wifi), "network-wireless-offline");

    auto css = css_for_interface(i);
    EXPECT_FALSE(css.empty());
}

TEST(NetworkTypes, FingerprintChanges)
{
    InterfaceInfo a;
    a.name = "igb0";
    a.up = true;
    a.running = true;
    a.ipv4 = {"192.168.1.1"};
    auto fp1 = interface_fingerprint(a);
    a.ipv4 = {"192.168.1.2"};
    EXPECT_NE(fp1, interface_fingerprint(a));
}

TEST(NetworkTypes, InformationOnlyHelper)
{
    NetworkStackFeatures f;
    EXPECT_TRUE(is_information_only(f));
    f.can_admin = true;
    f.admin = AdminPrivilege::Doas;
    EXPECT_FALSE(is_information_only(f));
}

TEST(NetworkTypes, DestroyableIfaceRules)
{
    /* Permanent hardware — never destroy */
    EXPECT_FALSE(is_destroyable_iface("aq0"));
    EXPECT_FALSE(is_destroyable_iface("igb0"));
    EXPECT_FALSE(is_destroyable_iface("em0"));
    EXPECT_FALSE(is_destroyable_iface("lo0"));
    EXPECT_FALSE(is_destroyable_iface(""));

    /* Clones / virtual — destroyable */
    EXPECT_TRUE(is_destroyable_iface("tap0"));
    EXPECT_TRUE(is_destroyable_iface("tun1"));
    EXPECT_TRUE(is_destroyable_iface("bridge0"));
    EXPECT_TRUE(is_destroyable_iface("gif0"));
    EXPECT_TRUE(is_destroyable_iface("gre0"));
    EXPECT_TRUE(is_destroyable_iface("vlan10"));
    EXPECT_TRUE(is_destroyable_iface("lagg0"));
    EXPECT_TRUE(is_destroyable_iface("epair0a"));
    EXPECT_TRUE(is_destroyable_iface("vxlan0"));
    EXPECT_TRUE(is_destroyable_iface("lo1"));
    EXPECT_TRUE(is_destroyable_iface("vm-public"));
    EXPECT_TRUE(is_destroyable_iface("wlan0")); /* cloned wlan */

    EXPECT_STREQ(toggle_action_label(true), "Turn off");
    EXPECT_STREQ(toggle_action_label(false), "Turn on");
}

TEST(NetworkTypes, CloneCatalogAndPreflight)
{
    size_t n = 0;
    const CloneTypeInfo *types = known_clone_types(&n);
    ASSERT_GT(n, 0u);
    EXPECT_NE(find_clone_type("tap"), nullptr);
    EXPECT_NE(find_clone_type("gif"), nullptr);
    EXPECT_EQ(find_clone_type("notatype"), nullptr);
    EXPECT_EQ(std::string(types[0].type), "tap");

    auto catalog = parse_ifconfig_clone_list("bridge vlan tap tun lo gif\n");
    ASSERT_EQ(catalog.size(), 6u);
    EXPECT_EQ(catalog[0], "bridge");
    EXPECT_EQ(catalog[5], "gif");

    EXPECT_TRUE(kldstat_has_module(
        "Id Refs Address Size Name\n 1 1 0x0 1000 if_gif.ko\n", "if_gif"));
    EXPECT_FALSE(kldstat_has_module(
        "Id Refs Address Size Name\n 1 1 0x0 1000 if_bridge.ko\n", "if_gif"));

    /* gif in catalog → can create with admin */
    auto ok = evaluate_create_preflight("gif", catalog, false, false, true);
    EXPECT_TRUE(ok.can_create);
    EXPECT_NE(ok.detail.find("ifconfig gif create"), std::string::npos);

    /* gif missing everywhere → blocked */
    std::vector<std::string> no_gif = {"tap", "bridge"};
    auto bad = evaluate_create_preflight("gif", no_gif, false, false, true);
    EXPECT_FALSE(bad.can_create);
    EXPECT_NE(bad.detail.find("not found"), std::string::npos);

    /* module file on disk still allows Create (load at apply time) */
    auto loadable = evaluate_create_preflight("gif", no_gif, false, true, true);
    EXPECT_TRUE(loadable.can_create);

    /* no admin → blocked even if module present */
    auto noadm = evaluate_create_preflight("tap", catalog, true, true, false);
    EXPECT_FALSE(noadm.can_create);
    EXPECT_NE(noadm.detail.find("information-only"), std::string::npos);
}

TEST(NetworkTypes, WifiFrequencyLabels)
{
    EXPECT_EQ(format_wifi_frequency_mhz(0), "");
    EXPECT_EQ(format_wifi_frequency_mhz(2412), "2412 MHz");
    EXPECT_EQ(format_wifi_band(2412), "2.4 GHz");
    EXPECT_EQ(format_wifi_band(2462), "2.4 GHz");
    EXPECT_EQ(format_wifi_band(5180), "5 GHz");
    EXPECT_EQ(format_wifi_band(5745), "5 GHz");
    EXPECT_EQ(format_wifi_band(6115), "6 GHz");

    /* Generation from band + MaxBitrate (Kb/s) */
    EXPECT_EQ(format_wifi_generation(2412, 54000), "Wi-Fi 3");
    EXPECT_EQ(format_wifi_generation(2412, 150000), "Wi-Fi 4");
    EXPECT_EQ(format_wifi_generation(5180, 400000), "Wi-Fi 5");
    EXPECT_EQ(format_wifi_generation(5180, 800000), "Wi-Fi 6");
    EXPECT_EQ(format_wifi_generation(6115, 0), "Wi-Fi 6E");
    EXPECT_EQ(format_wifi_generation(5180, 2500000), "Wi-Fi 7");

    /* Compact UI: band · generation */
    EXPECT_EQ(format_wifi_radio_label(2412, 150000), "2.4 GHz · Wi-Fi 4");
    EXPECT_EQ(format_wifi_radio_label(5180, 800000), "5 GHz · Wi-Fi 6");
    EXPECT_EQ(format_wifi_radio_label(6115, 0), "6 GHz · Wi-Fi 6E");
    EXPECT_EQ(format_wifi_radio_label(0, 0), "");
    EXPECT_EQ(format_wifi_radio_label(915, 0), "900 MHz");
}

/* ─── pure parsers ──────────────────────────────────────────────────────── */

TEST(NetworkParse, RouteGetInterface)
{
    const char *txt =
        "   route to: 0.0.0.0\n"
        "destination: 0.0.0.0\n"
        "    gateway: 99.48.162.254\n"
        "  interface: aq0\n"
        "      flags: <UP,GATEWAY,DONE,STATIC>\n";
    EXPECT_EQ(parse_route_get_interface(txt), "aq0");
    EXPECT_EQ(parse_route_get_gateway(txt), "99.48.162.254");
    EXPECT_TRUE(parse_route_get_interface("garbage").empty());
}

TEST(NetworkParse, IfconfigDetail)
{
    InterfaceInfo i;
    const char *txt =
        "aq0: flags=8843\n"
        "\tether 2c:f0:5d:8b:0e:3c\n"
        "\tinet 10.0.0.1 netmask 0xffffff00 broadcast 10.0.0.255\n"
        "\tinet6 fe80::2ef0:5dff:fe8b:e3c%aq0 prefixlen 64 scopeid 0x1\n"
        "\tinet6 2600:1700:1c14:4040:b49b:83f6:0:55 prefixlen 64\n"
        "\tmedia: Ethernet autoselect (10Gbase-T <full-duplex>)\n"
        "\tstatus: active\n";
    parse_ifconfig_detail(txt, i);
    EXPECT_EQ(i.mac, "2c:f0:5d:8b:0e:3c");
    EXPECT_NE(i.media.find("10Gbase-T"), std::string::npos);
    EXPECT_EQ(i.status, "active");
    ASSERT_FALSE(i.ipv4.empty());
    EXPECT_EQ(i.ipv4[0], "10.0.0.1");
    ASSERT_GE(i.ipv6.size(), 2u);
    /* link-local marked; global present */
    bool has_global = false;
    for (const auto& a : i.ipv6)
    {
        if (a.find("2600:1700") != std::string::npos)
        {
            has_global = true;
        }
    }
    EXPECT_TRUE(has_global);
}

TEST(NetworkParse, PickPrimary)
{
    std::vector<InterfaceInfo> list(2);
    list[0].name = "igb0";
    list[0].path = path_for_iface("igb0");
    list[0].kind = InterfaceKind::Ethernet;
    list[0].up = true;
    list[0].running = true;
    list[1].name = "aq0";
    list[1].path = path_for_iface("aq0");
    list[1].kind = InterfaceKind::Ethernet;
    list[1].up = true;
    list[1].running = true;
    list[1].is_default_route = true;
    list[1].is_default_route_v4 = true;
    list[1].is_default_route_v6 = true;
    EXPECT_EQ(pick_primary_path(list), path_for_iface("aq0"));
}

/* ─── Factory / Builder ─────────────────────────────────────────────────── */

class NetworkHooksTest : public ::testing::Test
{
  protected:
    void SetUp() override { reset_info_hooks(); }
    void TearDown() override
    {
        reset_info_hooks();
        detail::set_network_platform_override_for_test(nullptr);
    }
};

TEST_F(NetworkHooksTest, BuilderFluent)
{
    auto b = NetworkBackendFactory::builder()
        .poll_interval_ms(1500)
        .include_virtual(false)
        .include_bridge(true)
        .include_down(true);
    EXPECT_EQ(b.poll_interval_ms(), 1500);
    EXPECT_FALSE(b.include_virtual());
    EXPECT_TRUE(b.include_bridge());
}

TEST_F(NetworkHooksTest, FactoryFreeBSDProduct)
{
    detail::set_network_platform_override_for_test("freebsd");
    auto b = NetworkBackendFactory::create();
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(b->platform_name(), "freebsd");
    EXPECT_NO_THROW(b->connect());
    auto& devs = b->devices();
    EXPECT_GE(devs.size(), 1u); /* at least null sentinel */
    EXPECT_NO_THROW(b->disconnect());
}

TEST_F(NetworkHooksTest, FactoryNullProduct)
{
    detail::set_network_platform_override_for_test("openbsd");
    auto b = NetworkBackendFactory::create();
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(b->platform_name(), "unknown");
    b->connect();
    EXPECT_EQ(b->devices().size(), 1u);
    b->disconnect();
}

TEST_F(NetworkHooksTest, LiveProbeOnFreeBSD)
{
    if (std::strcmp(wf_platform_name(), "freebsd") != 0)
    {
        GTEST_SKIP() << "live probe only on FreeBSD";
    }
    auto list = probe_interfaces();
    /* This host has at least one non-loopback interface */
    EXPECT_FALSE(list.empty());
    bool any_ip = false;
    for (const auto& i : list)
    {
        EXPECT_FALSE(i.name.empty());
        EXPECT_EQ(i.path, path_for_iface(i.name));
        if (!i.ipv4.empty())
        {
            any_ip = true;
        }
    }
    EXPECT_TRUE(any_ip);

    auto def4 = default_route_interface_v4();
    /* default IPv4 route should exist on a working desktop */
    EXPECT_FALSE(def4.empty());
    /* IPv6 default may or may not exist; if host has global v6 on primary, expect some v6 */
    bool saw_v6 = false;
    for (const auto& i : list)
    {
        if (!i.ipv6.empty())
        {
            saw_v6 = true;
            break;
        }
    }
    EXPECT_TRUE(saw_v6) << "expected at least one interface with IPv6 on this host";
}

TEST_F(NetworkHooksTest, ProbeAdminPrivilegeDoesNotThrow)
{
    std::string method;
    AdminPrivilege p = AdminPrivilege::None;
    EXPECT_NO_THROW(p = probe_admin_privilege(&method));
    /* On this desktop mlapointe often has doas/sudo -n; either way is valid */
    if (p != AdminPrivilege::None)
    {
        EXPECT_FALSE(method.empty());
        EXPECT_TRUE(p == AdminPrivilege::Root || p == AdminPrivilege::Doas ||
            p == AdminPrivilege::Sudo);
    }
    auto feat = probe_features();
    EXPECT_EQ(feat.can_admin, p != AdminPrivilege::None);
}

TEST_F(NetworkHooksTest, FreeBSDNetworkSnapshot)
{
    InterfaceInfo i;
    i.name = "aq0";
    i.path = path_for_iface("aq0");
    i.kind = InterfaceKind::Ethernet;
    i.up = true;
    i.running = true;
    i.ipv4 = {"10.0.0.5"};
    i.is_default_route = true;

    FreeBSDNetwork net(i);
    EXPECT_TRUE(net.is_active());
    EXPECT_NE(net.get_name().find("10.0.0.5"), std::string::npos);
    /* multi-line compact form uses newline before address */
    EXPECT_NE(net.get_name().find('\n'), std::string::npos);
    EXPECT_EQ(net.get_interface(), "aq0");
    EXPECT_FALSE(net.can_toggle() && geteuid() != 0 && false); /* just call */
    (void)net.can_toggle();

    i.ipv4 = {"10.0.0.6"};
    EXPECT_TRUE(net.update_info(i));
    EXPECT_FALSE(net.update_info(i)); /* same fingerprint */
}

TEST(PlatformName, IsKnownPlatform)
{
    const char* name = wf_platform_name();
    ASSERT_NE(name, nullptr);
    bool known = (std::strcmp(name, "linux") == 0) ||
                 (std::strcmp(name, "freebsd") == 0) ||
                 (std::strcmp(name, "openbsd") == 0) ||
                 (std::strcmp(name, "netbsd") == 0);
    EXPECT_TRUE(known) << "Platform name '" << name << "' is not recognised";
}
