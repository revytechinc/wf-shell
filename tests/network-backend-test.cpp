#include <gtest/gtest.h>

#include "network/network-backend.hpp"
#include "network/network-info.hpp"
#include "network/network-types.hpp"
#include "network/freebsd-network.hpp"
#include "platform.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace wf_net;

/* ─── pure classification / display ─────────────────────────────────────── */

TEST(NetworkTypes, ClassifyIfaceNames)
{
    EXPECT_EQ(classify_iface_name(""), InterfaceKind::Other);
    EXPECT_EQ(classify_iface_name("wlan0"), InterfaceKind::Wireless);
    EXPECT_EQ(classify_iface_name("aq0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("igb0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("em0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("re0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("ue0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("vtnet0"), InterfaceKind::Ethernet);
    EXPECT_EQ(classify_iface_name("lo0"), InterfaceKind::Loopback);
    EXPECT_EQ(classify_iface_name("lo1"), InterfaceKind::Loopback);
    EXPECT_EQ(classify_iface_name("lo"), InterfaceKind::Loopback);
    EXPECT_EQ(classify_iface_name("bastille0"), InterfaceKind::Loopback);
    EXPECT_EQ(classify_iface_name("bridge0"), InterfaceKind::Bridge);
    EXPECT_EQ(classify_iface_name("vm-public"), InterfaceKind::Bridge);
    EXPECT_EQ(classify_iface_name("vm-port0"), InterfaceKind::Other); /* vm- prefix but not bridge */
    EXPECT_EQ(classify_iface_name("tap0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("tun0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("epair0a"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("gif0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("gre0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("lagg0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("wg0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("vnet0"), InterfaceKind::Virtual);
    EXPECT_EQ(classify_iface_name("weirdname"), InterfaceKind::Other);

    EXPECT_STREQ(kind_label(InterfaceKind::Ethernet), "Ethernet");
    EXPECT_STREQ(kind_label(InterfaceKind::Wireless), "Wireless");
    EXPECT_STREQ(kind_label(InterfaceKind::Bridge), "Bridge");
    EXPECT_STREQ(kind_label(InterfaceKind::Virtual), "Virtual");
    EXPECT_STREQ(kind_label(InterfaceKind::Loopback), "Loopback");
    EXPECT_STREQ(kind_label(InterfaceKind::Other), "Network");
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

    InterfaceInfo offline_br;
    offline_br.kind = InterfaceKind::Bridge;
    EXPECT_EQ(icon_for_interface(offline_br), "network-offline");
    InterfaceInfo offline_tap;
    offline_tap.kind = InterfaceKind::Virtual;
    EXPECT_EQ(icon_for_interface(offline_tap), "network-offline");
    InterfaceInfo other;
    other.kind = InterfaceKind::Other;
    other.up = true;
    other.running = true;
    EXPECT_EQ(icon_for_interface(other), "network-wired");
    other.running = false;
    EXPECT_EQ(icon_for_interface(other), "network-wired-disconnected");

    /* offline label when no addrs */
    InterfaceInfo down;
    down.name = "em1";
    down.running = false;
    EXPECT_EQ(format_display_name(down), "em1 · offline");
    EXPECT_TRUE(format_address_summary(down).empty());

    auto css = css_for_interface(i);
    EXPECT_FALSE(css.empty());
    EXPECT_NE(std::find(css.begin(), css.end(), "ethernet"), css.end());

    InterfaceInfo wifi_css;
    wifi_css.kind = InterfaceKind::Wireless;
    wifi_css.up = true;
    wifi_css.running = true;
    wifi_css.is_default_route = true;
    auto wcss = css_for_interface(wifi_css);
    EXPECT_NE(std::find(wcss.begin(), wcss.end(), "wifi"), wcss.end());
    EXPECT_NE(std::find(wcss.begin(), wcss.end(), "excellent"), wcss.end());

    wifi_css.is_default_route = false;
    wcss = css_for_interface(wifi_css);
    EXPECT_NE(std::find(wcss.begin(), wcss.end(), "good"), wcss.end());

    wifi_css.running = false;
    wcss = css_for_interface(wifi_css);
    EXPECT_NE(std::find(wcss.begin(), wcss.end(), "medium"), wcss.end());

    wifi_css.up = false;
    wcss = css_for_interface(wifi_css);
    EXPECT_NE(std::find(wcss.begin(), wcss.end(), "none"), wcss.end());

    InterfaceInfo lo_css;
    lo_css.kind = InterfaceKind::Loopback;
    auto lcss = css_for_interface(lo_css);
    EXPECT_NE(std::find(lcss.begin(), lcss.end(), "none"), lcss.end());

    InterfaceInfo br_css;
    br_css.kind = InterfaceKind::Bridge;
    br_css.up = true;
    br_css.running = true;
    auto bcss = css_for_interface(br_css);
    EXPECT_NE(std::find(bcss.begin(), bcss.end(), "ethernet"), bcss.end());

    InterfaceInfo tap_css;
    tap_css.kind = InterfaceKind::Virtual;
    tap_css.up = false;
    auto tcss = css_for_interface(tap_css);
    EXPECT_NE(std::find(tcss.begin(), tcss.end(), "ethernet"), tcss.end());
    EXPECT_NE(std::find(tcss.begin(), tcss.end(), "none"), tcss.end());
}

TEST(NetworkTypes, FingerprintChanges)
{
    InterfaceInfo a;
    a.name = "igb0";
    a.up = true;
    a.running = true;
    a.ipv4 = {"192.168.1.1"};
    a.ipv6 = {"2001:db8::1"};
    a.is_default_route = true;
    a.is_default_route_v4 = true;
    a.is_default_route_v6 = false;
    a.gateway_v4 = "192.168.1.254";
    a.media = "1000baseT";
    a.status = "active";
    auto fp1 = interface_fingerprint(a);
    EXPECT_EQ(fp1, interface_fingerprint(a));
    a.ipv4 = {"192.168.1.2"};
    EXPECT_NE(fp1, interface_fingerprint(a));
    a.ipv4 = {"192.168.1.1"};
    a.up = false;
    EXPECT_NE(fp1, interface_fingerprint(a));
    EXPECT_EQ(path_for_iface("aq0"), "/freebsd/interfaces/aq0");
}

TEST(NetworkTypes, InformationOnlyHelper)
{
    NetworkStackFeatures f;
    EXPECT_TRUE(is_information_only(f));
    EXPECT_FALSE(needs_admin_password(f));

    f.can_admin = true;
    f.admin = AdminPrivilege::Doas;
    EXPECT_FALSE(is_information_only(f));
    EXPECT_FALSE(needs_admin_password(f));

    /* doas/sudo installed but password required — still offer mutations */
    f.admin = AdminPrivilege::NeedsPassword;
    f.needs_password = true;
    f.admin_method = "doas";
    EXPECT_FALSE(is_information_only(f));
    EXPECT_TRUE(needs_admin_password(f));
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
    EXPECT_TRUE(is_destroyable_iface("vm-port0"));
    EXPECT_TRUE(is_destroyable_iface("wlan0")); /* cloned wlan */
    EXPECT_TRUE(is_destroyable_iface("wg0"));
    EXPECT_TRUE(is_destroyable_iface("stf0"));
    EXPECT_TRUE(is_destroyable_iface("vmnet0"));
    EXPECT_TRUE(is_destroyable_iface("epair0b"));
    EXPECT_FALSE(is_destroyable_iface("notaclone"));
    EXPECT_FALSE(is_destroyable_iface("tap")); /* no unit number */

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
    EXPECT_NE(find_clone_type("lo"), nullptr); /* nullptr module */
    EXPECT_EQ(find_clone_type("notatype"), nullptr);
    EXPECT_EQ(std::string(types[0].type), "tap");
    /* known_clone_types with null count still returns table */
    EXPECT_NE(known_clone_types(nullptr), nullptr);

    auto catalog = parse_ifconfig_clone_list("bridge vlan tap tun lo gif\n");
    ASSERT_EQ(catalog.size(), 6u);
    EXPECT_EQ(catalog[0], "bridge");
    EXPECT_EQ(catalog[5], "gif");
    EXPECT_TRUE(parse_ifconfig_clone_list("").empty());
    EXPECT_TRUE(parse_ifconfig_clone_list("   \n").empty());
    auto spaced = parse_ifconfig_clone_list("  tap   bridge  \t gif ");
    ASSERT_EQ(spaced.size(), 3u);
    /* trailing token with no trailing whitespace */
    auto one = parse_ifconfig_clone_list("tap");
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], "tap");
    auto two = parse_ifconfig_clone_list("tap\nbridge");
    ASSERT_EQ(two.size(), 2u);

    EXPECT_TRUE(kldstat_has_module(
        "Id Refs Address Size Name\n 1 1 0x0 1000 if_gif.ko\n", "if_gif"));
    EXPECT_TRUE(kldstat_has_module("if_gif", "if_gif")); /* bare token */
    EXPECT_FALSE(kldstat_has_module(
        "Id Refs Address Size Name\n 1 1 0x0 1000 if_bridge.ko\n", "if_gif"));
    EXPECT_FALSE(kldstat_has_module("", "if_gif"));
    EXPECT_FALSE(kldstat_has_module("x", ""));
    EXPECT_FALSE(kldstat_has_module("myif_gif_extra.ko", "if_gif")); /* not token */

    auto unknown = evaluate_create_preflight("nope", catalog, true, true, true);
    EXPECT_FALSE(unknown.can_create);
    EXPECT_EQ(unknown.detail, "unknown_type");

    /* gif in catalog → can create; success detail stays empty (no UI narration) */
    auto ok = evaluate_create_preflight("gif", catalog, false, false, true);
    EXPECT_TRUE(ok.can_create);
    EXPECT_TRUE(ok.detail.empty());
    EXPECT_EQ(ok.module, "if_gif");

    /* gif missing everywhere → blocked (omit from UI; code has fail token only) */
    std::vector<std::string> no_gif = {"tap", "bridge"};
    auto bad = evaluate_create_preflight("gif", no_gif, false, false, true);
    EXPECT_FALSE(bad.can_create);
    EXPECT_EQ(bad.detail, "module_unavailable");

    /* module loaded without catalog */
    auto loaded = evaluate_create_preflight("gif", no_gif, true, false, true);
    EXPECT_TRUE(loaded.can_create);

    /* module file on disk still allows Create (load at apply time) */
    auto loadable = evaluate_create_preflight("gif", no_gif, false, true, true);
    EXPECT_TRUE(loadable.can_create);
    EXPECT_TRUE(loadable.detail.empty());

    /* no admin → blocked even if module present */
    auto noadm = evaluate_create_preflight("tap", catalog, true, true, false);
    EXPECT_FALSE(noadm.can_create);
    EXPECT_EQ(noadm.detail, "no_admin");

    /* lo: no module; in catalog */
    auto lo_ok = evaluate_create_preflight("lo", catalog, false, false, true);
    EXPECT_TRUE(lo_ok.can_create);
    EXPECT_TRUE(lo_ok.module.empty());

    /* lo: not in non-empty catalog without module → blocked */
    auto lo_bad = evaluate_create_preflight("lo", no_gif, false, false, true);
    EXPECT_FALSE(lo_bad.can_create);
    EXPECT_EQ(lo_bad.detail, "not_in_catalog");

    /* empty catalog + no module type → optimistic create */
    std::vector<std::string> empty;
    auto lo_empty = evaluate_create_preflight("lo", empty, false, false, true);
    EXPECT_TRUE(lo_empty.can_create);
}

TEST(NetworkTypes, WifiFrequencyLabels)
{
    EXPECT_EQ(format_wifi_frequency_mhz(0), "");
    EXPECT_EQ(format_wifi_frequency_mhz(2412), "2412 MHz");
    EXPECT_EQ(format_wifi_band(0), "");
    EXPECT_EQ(format_wifi_band(915), "900 MHz");
    EXPECT_EQ(format_wifi_band(2412), "2.4 GHz");
    EXPECT_EQ(format_wifi_band(2462), "2.4 GHz");
    EXPECT_EQ(format_wifi_band(5180), "5 GHz");
    EXPECT_EQ(format_wifi_band(5745), "5 GHz");
    EXPECT_EQ(format_wifi_band(6115), "6 GHz");
    EXPECT_EQ(format_wifi_band(45000), "45 GHz");
    EXPECT_EQ(format_wifi_band(60000), "60 GHz");
    EXPECT_EQ(format_wifi_band(1234), ""); /* unknown band */

    EXPECT_EQ(format_wifi_generation(0, 0), "");
    EXPECT_EQ(format_wifi_generation(2412, 54000), "Wi-Fi 3");
    EXPECT_EQ(format_wifi_generation(2412, 150000), "Wi-Fi 4");
    EXPECT_EQ(format_wifi_generation(5180, 400000), "Wi-Fi 5");
    EXPECT_EQ(format_wifi_generation(5180, 100000), "Wi-Fi 5"); /* band+bitrate */
    EXPECT_EQ(format_wifi_generation(5180, 800000), "Wi-Fi 6");
    EXPECT_EQ(format_wifi_generation(6115, 150000), "Wi-Fi 6E");
    EXPECT_EQ(format_wifi_generation(6115, 0), "Wi-Fi 6E");
    EXPECT_EQ(format_wifi_generation(5180, 2500000), "Wi-Fi 7");
    EXPECT_EQ(format_wifi_generation(6115, 2500000), "Wi-Fi 7");
    EXPECT_EQ(format_wifi_generation(5180, 0), "Wi-Fi 5"); /* band-only 5 */
    EXPECT_EQ(format_wifi_generation(2412, 0), "Wi-Fi 4"); /* band-only 2.4 */
    EXPECT_EQ(format_wifi_generation(900, 10000), ""); /* no band match */

    EXPECT_EQ(format_wifi_radio_label(2412, 150000), "2.4 GHz · Wi-Fi 4");
    EXPECT_EQ(format_wifi_radio_label(5180, 800000), "5 GHz · Wi-Fi 6");
    EXPECT_EQ(format_wifi_radio_label(6115, 0), "6 GHz · Wi-Fi 6E");
    EXPECT_EQ(format_wifi_radio_label(0, 0), "");
    EXPECT_EQ(format_wifi_radio_label(915, 0), "900 MHz");
    EXPECT_EQ(format_wifi_radio_label(0, 800000), "Wi-Fi 6"); /* gen only */
    EXPECT_EQ(format_wifi_radio_label(1234, 0), "1234 MHz"); /* freq fallback */
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
    EXPECT_TRUE(parse_route_get_gateway("").empty());
    EXPECT_TRUE(default_route_interface().empty() || !default_route_interface().empty());
}

TEST(NetworkParse, IfconfigDetail)
{
    InterfaceInfo i;
    const char *txt =
        "aq0: flags=8843\n"
        "\tether 2c:f0:5d:8b:0e:3c\n"
        "\tinet 10.0.0.1 netmask 0xffffff00 broadcast 10.0.0.255\n"
        "\tinet 10.0.0.1 netmask 0xffffff00\n" /* duplicate push_unique */
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
    EXPECT_EQ(i.ipv4.size(), 1u); /* deduped */
    ASSERT_GE(i.ipv6.size(), 2u);
    bool has_global = false;
    bool has_ll_mark = false;
    for (const auto& a : i.ipv6)
    {
        if (a.find("2600:1700") != std::string::npos)
        {
            has_global = true;
        }
        if (a.find("%ll") != std::string::npos)
        {
            has_ll_mark = true;
        }
    }
    EXPECT_TRUE(has_global);
    EXPECT_TRUE(has_ll_mark);

    InterfaceInfo empty;
    parse_ifconfig_detail("", empty);
    EXPECT_TRUE(empty.mac.empty());
    parse_ifconfig_detail("\tether aa:bb:cc:dd:ee:ff\n", empty);
    EXPECT_EQ(empty.mac, "aa:bb:cc:dd:ee:ff");
    /* second ether ignored */
    parse_ifconfig_detail("\tether 11:22:33:44:55:66\n", empty);
    EXPECT_EQ(empty.mac, "aa:bb:cc:dd:ee:ff");

    InterfaceInfo v6only;
    parse_ifconfig_detail(
        "\tinet6 fe80::abcd prefixlen 64\n" /* link-local no %iface in ifconfig */
        "\tinet6 2001:db8::2 prefixlen 64\n",
        v6only);
    bool saw_ll = false;
    for (const auto& a : v6only.ipv6)
    {
        if (a.find("%ll") != std::string::npos)
        {
            saw_ll = true;
        }
    }
    EXPECT_TRUE(saw_ll);
}

TEST(NetworkParse, PickPrimary)
{
    EXPECT_TRUE(pick_primary_path({}).empty());

    std::vector<InterfaceInfo> list(4);
    list[0].name = "lo0";
    list[0].path = path_for_iface("lo0");
    list[0].kind = InterfaceKind::Loopback;
    list[0].up = true;
    list[0].running = true;

    list[1].name = "tap0";
    list[1].path = path_for_iface("tap0");
    list[1].kind = InterfaceKind::Virtual;
    list[1].up = true;
    list[1].running = true;

    list[2].name = "igb0";
    list[2].path = path_for_iface("igb0");
    list[2].kind = InterfaceKind::Ethernet;
    list[2].up = true;
    list[2].running = true;

    list[3].name = "aq0";
    list[3].path = path_for_iface("aq0");
    list[3].kind = InterfaceKind::Ethernet;
    list[3].up = true;
    list[3].running = true;
    list[3].is_default_route = true;
    list[3].is_default_route_v4 = true;
    list[3].is_default_route_v6 = true;
    EXPECT_EQ(pick_primary_path(list), path_for_iface("aq0")); /* dual-stack default */

    list[3].is_default_route_v6 = false;
    EXPECT_EQ(pick_primary_path(list), path_for_iface("aq0")); /* v4 default */

    list[3].is_default_route_v4 = false;
    list[3].is_default_route_v6 = true;
    EXPECT_EQ(pick_primary_path(list), path_for_iface("aq0")); /* v6 default */

    list[3].is_default_route_v6 = false;
    list[3].is_default_route = false;
    list[3].up = false;
    list[3].running = false;
    EXPECT_EQ(pick_primary_path(list), path_for_iface("igb0")); /* first physical running */

    list[2].up = false;
    list[2].running = false;
    /* skip loopback in physical pass → first remaining running is lo0 (order) */
    EXPECT_EQ(pick_primary_path(list), path_for_iface("lo0"));

    list[0].up = false;
    list[0].running = false;
    EXPECT_EQ(pick_primary_path(list), path_for_iface("tap0")); /* virtual still running */

    list[1].up = false;
    list[1].running = false;
    EXPECT_TRUE(pick_primary_path(list).empty());
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
        .include_down(true)
        .include_loopback(true);
    EXPECT_EQ(b.poll_interval_ms(), 1500);
    EXPECT_FALSE(b.include_virtual());
    EXPECT_TRUE(b.include_bridge());
    EXPECT_TRUE(b.include_down());
    EXPECT_TRUE(b.include_loopback());
    /* non-positive interval falls back */
    auto b2 = NetworkBackendFactory::builder().poll_interval_ms(0);
    EXPECT_EQ(b2.poll_interval_ms(), 3000);
    auto b3 = NetworkBackendFactory::builder().poll_interval_ms(-5);
    EXPECT_EQ(b3.poll_interval_ms(), 3000);
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

TEST_F(NetworkHooksTest, FactoryUsesHostPlatformWhenNoOverride)
{
    /* override null — exercises effective_platform() → wf_platform_name() */
    detail::set_network_platform_override_for_test(nullptr);
    auto b = NetworkBackendFactory::create();
    ASSERT_NE(b, nullptr);
    if (std::strcmp(wf_platform_name(), "freebsd") == 0)
    {
        EXPECT_STREQ(b->platform_name(), "freebsd");
    }
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
    if (p != AdminPrivilege::None)
    {
        EXPECT_FALSE(method.empty());
        EXPECT_TRUE(p == AdminPrivilege::Root || p == AdminPrivilege::Doas ||
            p == AdminPrivilege::Sudo || p == AdminPrivilege::NeedsPassword);
    }
    auto feat = probe_features();
    EXPECT_EQ(feat.can_admin, p != AdminPrivilege::None);
    EXPECT_EQ(feat.needs_password, p == AdminPrivilege::NeedsPassword);
    if (p == AdminPrivilege::NeedsPassword)
    {
        EXPECT_TRUE(needs_admin_password(feat));
        EXPECT_FALSE(is_information_only(feat));
    }
}

TEST_F(NetworkHooksTest, ProbeAdminViaHooks)
{
    /* doas -n succeeds */
    info_hooks().run_cmd = [] (const std::string& cmd) -> std::string {
        if (cmd.find("doas -n") != std::string::npos)
        {
            return "ok";
        }
        return {};
    };
    std::string method;
    EXPECT_EQ(probe_admin_privilege(&method), AdminPrivilege::Doas);
    EXPECT_EQ(method, "doas");

    /* sudo -n only */
    info_hooks().run_cmd = [] (const std::string& cmd) -> std::string {
        if (cmd.find("doas -n") != std::string::npos)
        {
            return {};
        }
        if (cmd.find("sudo -n") != std::string::npos)
        {
            return "ok";
        }
        return {};
    };
    EXPECT_EQ(probe_admin_privilege(&method), AdminPrivilege::Sudo);
    EXPECT_EQ(method, "sudo");

    /* doas present, needs password */
    info_hooks().run_cmd = [] (const std::string& cmd) -> std::string {
        if (cmd.find("doas -n") != std::string::npos ||
            cmd.find("sudo -n") != std::string::npos)
        {
            return {};
        }
        if (cmd.find("command -v doas") != std::string::npos)
        {
            return "ok";
        }
        return {};
    };
    EXPECT_EQ(probe_admin_privilege(&method), AdminPrivilege::NeedsPassword);
    EXPECT_EQ(method, "doas");

    /* only sudo binary */
    info_hooks().run_cmd = [] (const std::string& cmd) -> std::string {
        if (cmd.find("-n") != std::string::npos)
        {
            return {};
        }
        if (cmd.find("command -v doas") != std::string::npos)
        {
            return {};
        }
        if (cmd.find("command -v sudo") != std::string::npos)
        {
            return "ok";
        }
        return {};
    };
    EXPECT_EQ(probe_admin_privilege(&method), AdminPrivilege::NeedsPassword);
    EXPECT_EQ(method, "sudo");

    /* nothing */
    info_hooks().run_cmd = [] (const std::string&) -> std::string { return {}; };
    EXPECT_EQ(probe_admin_privilege(&method), AdminPrivilege::None);
    EXPECT_TRUE(method.empty());
    EXPECT_EQ(probe_admin_privilege(nullptr), AdminPrivilege::None);
}

TEST_F(NetworkHooksTest, ProbeFeaturesAndCreateWithHooks)
{
    info_hooks().run_cmd = [] (const std::string& cmd) -> std::string {
        if (cmd.find("route -n get default") != std::string::npos &&
            cmd.find("inet6") == std::string::npos)
        {
            return "  interface: aq0\n    gateway: 10.0.0.1\n";
        }
        if (cmd.find("route -n get -inet6") != std::string::npos)
        {
            return "  interface: aq0\n    gateway: fe80::1\n";
        }
        if (cmd.find("ifconfig -C") != std::string::npos)
        {
            return "bridge vlan tap tun lo gif\n";
        }
        if (cmd.find("kldstat") != std::string::npos)
        {
            return "Id Refs Name\n 1 1 if_bridge.ko\n";
        }
        if (cmd.find("doas -n") != std::string::npos)
        {
            return "ok";
        }
        if (cmd.find("command -v wpa_cli") != std::string::npos)
        {
            return "ok";
        }
        if (cmd.rfind("ifconfig ", 0) == 0)
        {
            return "aq0: flags=8843\n\tstatus: active\n\tmedia: Ethernet\n";
        }
        return {};
    };

    EXPECT_EQ(default_route_interface_v4(), "aq0");
    EXPECT_EQ(default_route_interface_v6(), "aq0");

    auto pf_gif = probe_create_preflight("gif");
    /* gif in -C catalog → can create with admin */
    EXPECT_TRUE(pf_gif.can_create);
    EXPECT_TRUE(pf_gif.detail.empty());

    auto pf_unknown = probe_create_preflight("notatype");
    EXPECT_FALSE(pf_unknown.can_create);

    auto catalog = probe_create_catalog();
    EXPECT_FALSE(catalog.empty());
    bool any_ok = false;
    for (const auto& c : catalog)
    {
        if (c.can_create)
        {
            any_ok = true;
        }
    }
    EXPECT_TRUE(any_ok);

    /* Features still uses live getifaddrs; just ensure no throw with hooks */
    EXPECT_NO_THROW({
        auto f = probe_features();
        EXPECT_TRUE(f.can_admin);
        EXPECT_FALSE(f.needs_password);
        EXPECT_EQ(f.admin_method, "doas");
    });
}

TEST_F(NetworkHooksTest, ProbeCreateNoAdmin)
{
    info_hooks().run_cmd = [] (const std::string& cmd) -> std::string {
        if (cmd.find("ifconfig -C") != std::string::npos)
        {
            return "tap bridge gif\n";
        }
        return {}; /* no admin elevators */
    };
    auto pf = probe_create_preflight("tap");
    EXPECT_FALSE(pf.can_create);
    EXPECT_EQ(pf.detail, "no_admin");
}

TEST(NetworkTypes, InputValidation)
{
    EXPECT_TRUE(validation_ok().ok);
    EXPECT_FALSE(validation_fail("x").ok);
    EXPECT_EQ(validation_fail("x").message, "x");

    EXPECT_TRUE(validate_iface_name("tap0").ok);
    EXPECT_TRUE(validate_iface_name("  bridge1  ", true).ok);
    EXPECT_TRUE(validate_iface_name("", true).ok);
    EXPECT_FALSE(validate_iface_name("").ok);
    EXPECT_FALSE(validate_iface_name("1tap").ok);
    EXPECT_FALSE(validate_iface_name("tap;rm").ok);
    EXPECT_FALSE(validate_iface_name("tap/x").ok);
    EXPECT_FALSE(validate_iface_name("a..b").ok);
    EXPECT_FALSE(validate_iface_name(std::string(16, 'a')).ok);
    EXPECT_TRUE(validate_iface_name(std::string(15, 'a')).ok);

    EXPECT_TRUE(validate_ipv4_address("192.168.1.10").ok);
    EXPECT_TRUE(validate_ipv4_address("0.0.0.0").ok);
    EXPECT_TRUE(validate_ipv4_address("255.255.255.255").ok);
    EXPECT_FALSE(validate_ipv4_address("192.168.1.256").ok);
    EXPECT_FALSE(validate_ipv4_address("1.2.3").ok);
    EXPECT_FALSE(validate_ipv4_address("1.2.3.4.5").ok);
    EXPECT_FALSE(validate_ipv4_address("1.2.3.").ok);
    EXPECT_FALSE(validate_ipv4_address(".1.2.3").ok);
    EXPECT_FALSE(validate_ipv4_address("a.b.c.d").ok);
    EXPECT_FALSE(validate_ipv4_address("").ok);
    EXPECT_TRUE(validate_ipv4_address("", true).ok);
    EXPECT_FALSE(validate_ipv4_address("1.2..3").ok);
    EXPECT_TRUE(validate_ipv4_address("01.2.3.04").ok); /* leading zeros ok */
    EXPECT_FALSE(validate_ipv4_address("999.1.1.1").ok);
    EXPECT_FALSE(validate_ipv4_address("1.1.1.9999").ok);
    EXPECT_FALSE(validate_ipv4_address("1.2.3.4extra").ok);
    EXPECT_FALSE(validate_ipv4_address("1234.1.1.1").ok);

    EXPECT_TRUE(validate_ipv6_address("2001:db8::1").ok);
    EXPECT_TRUE(validate_ipv6_address("::1").ok);
    EXPECT_TRUE(validate_ipv6_address("fe80::1%aq0").ok);
    EXPECT_TRUE(validate_ipv6_address("::ffff:192.0.2.1").ok);
    EXPECT_TRUE(validate_ipv6_address("2001:0db8:85a3:0000:0000:8a2e:0370:7334").ok);
    EXPECT_FALSE(validate_ipv6_address("gggg::1").ok);
    EXPECT_FALSE(validate_ipv6_address("fe80::1%").ok);
    EXPECT_FALSE(validate_ipv6_address("fe80::1%1bad").ok);
    EXPECT_FALSE(validate_ipv6_address("2001::db8::1").ok); /* double :: */
    EXPECT_FALSE(validate_ipv6_address("").ok);
    EXPECT_TRUE(validate_ipv6_address("", true).ok);
    EXPECT_FALSE(validate_ipv6_address("notahex").ok);
    EXPECT_FALSE(validate_ipv6_address("2001:db8:1:2:3:4:5:6:7").ok); /* too many */
    EXPECT_FALSE(validate_ipv6_address(":1").ok); /* lone leading colon */
    EXPECT_FALSE(validate_ipv6_address("1:").ok);
    EXPECT_FALSE(validate_ipv6_address("2001:db8:1:2:3:4:5:zzzz").ok);
    EXPECT_FALSE(validate_ipv6_address("2001:db8:1:2:3:4:5:6:7:8").ok);
    EXPECT_FALSE(validate_ipv6_address("::ffff:999.0.0.1").ok);
    EXPECT_TRUE(validate_ipv6_address("2001:db8:85a3::8a2e:370:7334").ok);
    EXPECT_FALSE(validate_ipv6_address("12345::1").ok); /* hextet too long */
    EXPECT_FALSE(validate_ipv6_address("%aq0").ok);
    EXPECT_FALSE(validate_ipv6_address("deadbeef").ok); /* hex only, no colon */
    EXPECT_FALSE(validate_ipv6_address("1:2:3:4:5:6:7:8::9").ok); /* too many with :: */

    EXPECT_TRUE(validate_prefix_length("0", 32).ok);
    EXPECT_TRUE(validate_prefix_length("24", 32).ok);
    EXPECT_TRUE(validate_prefix_length("32", 32).ok);
    EXPECT_FALSE(validate_prefix_length("33", 32).ok);
    EXPECT_FALSE(validate_prefix_length("-1", 32).ok);
    EXPECT_FALSE(validate_prefix_length("xx", 32).ok);
    EXPECT_FALSE(validate_prefix_length("", 32).ok);
    EXPECT_TRUE(validate_prefix_length("", 32, true).ok);
    EXPECT_TRUE(validate_prefix_length("64", 128).ok);
    EXPECT_FALSE(validate_prefix_length("129", 128).ok);
    EXPECT_FALSE(validate_prefix_length("1", 0).ok); /* bad max */
    EXPECT_FALSE(validate_prefix_length("9999", 128).ok);

    EXPECT_FALSE(validate_admin_password("").ok);
    EXPECT_TRUE(validate_admin_password("secret").ok);
    EXPECT_FALSE(validate_admin_password(std::string(513, 'x')).ok);
    EXPECT_TRUE(validate_admin_password(std::string(512, 'x')).ok);

    ConfigFormInput cfg;
    cfg.v4_mode = "static";
    cfg.v4_addr = "10.0.0.1";
    cfg.v4_prefix = "24";
    cfg.v4_gateway = "10.0.0.254";
    cfg.v6_mode = "accept_rtadv";
    EXPECT_TRUE(validate_config_form(cfg).ok);

    cfg.v4_addr = "not-an-ip";
    auto bad4 = validate_config_form(cfg);
    EXPECT_FALSE(bad4.ok);
    EXPECT_FALSE(bad4.v4_addr.empty());

    cfg.v4_addr = "10.0.0.1";
    cfg.v4_prefix = "99";
    EXPECT_FALSE(validate_config_form(cfg).ok);

    cfg.v4_prefix = "24";
    cfg.v4_gateway = "bad";
    EXPECT_FALSE(validate_config_form(cfg).ok);

    cfg.v4_mode = "bogus";
    cfg.v4_gateway.clear();
    EXPECT_FALSE(validate_config_form(cfg).ok);

    cfg.v4_mode = "dhcp";
    cfg.v4_addr.clear();
    cfg.v6_mode = "static";
    cfg.v6_addr = "2001:db8::10";
    cfg.v6_prefix = "64";
    cfg.v6_gateway = "fe80::1%aq0";
    EXPECT_TRUE(validate_config_form(cfg).ok);

    cfg.v6_addr = ":::";
    EXPECT_FALSE(validate_config_form(cfg).ok);
    cfg.v6_addr = "2001:db8::10";
    cfg.v6_prefix = "200";
    EXPECT_FALSE(validate_config_form(cfg).ok);
    cfg.v6_prefix = "64";
    cfg.v6_gateway = "not-v6";
    EXPECT_FALSE(validate_config_form(cfg).ok);
    cfg.v6_gateway.clear();
    cfg.v6_mode = "weird";
    EXPECT_FALSE(validate_config_form(cfg).ok);

    cfg.v6_mode = "none";
    cfg.v4_mode = "none";
    EXPECT_TRUE(validate_config_form(cfg).ok);

    CreateFormInput cr;
    cr.type = "tap";
    cr.name = "";
    EXPECT_TRUE(validate_create_form(cr, {"aq0"}).ok);
    cr.name = "  ";
    EXPECT_TRUE(validate_create_form(cr, {"aq0"}).ok);
    cr.name = "aq0";
    auto taken = validate_create_form(cr, {"aq0", "tap0"});
    EXPECT_FALSE(taken.ok);
    EXPECT_FALSE(taken.name.empty());
    cr.name = "bad;name";
    EXPECT_FALSE(validate_create_form(cr, {}).ok);
    cr.type = "nope";
    cr.name = "tap9";
    auto badt = validate_create_form(cr, {});
    EXPECT_FALSE(badt.ok);
    EXPECT_FALSE(badt.type.empty());
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
    EXPECT_NE(net.get_name().find('\n'), std::string::npos);
    EXPECT_EQ(net.get_interface(), "aq0");
    EXPECT_NE(net.get_friendly_name().find("Ethernet"), std::string::npos);
    EXPECT_NE(net.get_friendly_name().find("aq0"), std::string::npos);
    EXPECT_FALSE(net.get_icon_name().empty());
    EXPECT_FALSE(net.get_css_classes().empty());
    if (geteuid() == 0)
    {
        EXPECT_TRUE(net.can_toggle());
    }
    else
    {
        EXPECT_FALSE(net.can_toggle());
    }

    i.ipv4 = {"10.0.0.6"};
    EXPECT_TRUE(net.update_info(i));
    EXPECT_FALSE(net.update_info(i)); /* same fingerprint */

    /* disconnect fail-soft on a non-existent unit (does not touch real NICs) */
    InterfaceInfo ghost;
    ghost.name = "tap99999";
    ghost.path = path_for_iface("tap99999");
    ghost.kind = InterfaceKind::Virtual;
    ghost.up = true;
    ghost.running = true;
    FreeBSDNetwork gnet(ghost);
    EXPECT_TRUE(gnet.is_active());
    gnet.disconnect();
    EXPECT_FALSE(gnet.is_active());
}

TEST_F(NetworkHooksTest, FactoryLinuxNullProduct)
{
    detail::set_network_platform_override_for_test("linux");
    auto b = NetworkBackendFactory::create();
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(b->platform_name(), "unknown");
}

TEST_F(NetworkHooksTest, ProbeOptionsFilter)
{
    if (std::strcmp(wf_platform_name(), "freebsd") != 0)
    {
        GTEST_SKIP() << "live filter only on FreeBSD";
    }
    ProbeOptions no_virt;
    no_virt.include_virtual = false;
    no_virt.include_bridge = false;
    no_virt.include_loopback = false;
    auto list = probe_interfaces(no_virt);
    for (const auto& i : list)
    {
        EXPECT_NE(i.kind, InterfaceKind::Virtual);
        EXPECT_NE(i.kind, InterfaceKind::Bridge);
        EXPECT_NE(i.kind, InterfaceKind::Loopback);
    }

    ProbeOptions only_up;
    only_up.include_down = false;
    only_up.include_loopback = true;
    auto up_only = probe_interfaces(only_up);
    for (const auto& i : up_only)
    {
        EXPECT_TRUE(i.up) << i.name;
    }
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
