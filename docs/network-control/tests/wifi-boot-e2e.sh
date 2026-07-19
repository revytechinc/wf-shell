#!/bin/sh
# End-to-end: FreeBSD Wi-Fi is permanent system config (rc.conf + wpa).
# Simulates multiuser boot: destroy wlan clone, recreate from sysrc, start
# wpa_supplicant from /etc/wpa_supplicant.conf, renew DHCP, verify address.
#
# Usage: docs/network-control/tests/wifi-boot-e2e.sh
# Requires: doas -n or sudo -n, wpa_cli, sysrc, dhclient
set -eu

pass=0
fail=0
ok()  { echo "PASS: $*"; pass=$((pass + 1)); }
bad() { echo "FAIL: $*"; fail=$((fail + 1)); }

elev() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif doas -n true >/dev/null 2>&1; then
    doas -n "$@"
  elif sudo -n true >/dev/null 2>&1; then
    sudo -n "$@"
  else
    echo "NO_ELEV: need doas -n or sudo -n"
    return 1
  fi
}

PARENT="${WIFI_E2E_PARENT:-iwlwifi0}"
WLAN="${WIFI_E2E_WLAN:-wlan0}"
CONF="${WIFI_E2E_WPA_CONF:-/etc/wpa_supplicant.conf}"

echo "======== WIFI BOOT E2E $(date -u +%Y-%m-%dT%H:%M:%SZ) parent=$PARENT wlan=$WLAN ========"

elev true && ok "elevation" || { bad "elevation"; exit 1; }

# --- rc.conf permanent keys ---
WLANS=$(sysrc -n "wlans_${PARENT}" 2>/dev/null || true)
IFC=$(sysrc -n "ifconfig_${WLAN}" 2>/dev/null || true)
echo "rc: wlans_${PARENT}=$WLANS ifconfig_${WLAN}=$IFC"
echo "$WLANS" | grep -qw "$WLAN" && ok "wlans lists $WLAN" || bad "wlans_${PARENT} missing $WLAN"
echo "$IFC" | grep -q WPA && ok "ifconfig has WPA" || bad "ifconfig missing WPA"
echo "$IFC" | grep -Eq 'DHCP|SYNCDHCP' && ok "ifconfig has DHCP/SYNCDHCP" || bad "ifconfig missing DHCP"
elev test -s "$CONF" && ok "wpa conf exists" || bad "no wpa conf"

# --- baseline associated? ---
if wpa_cli -i "$WLAN" status 2>/dev/null | grep -q 'wpa_state=COMPLETED'; then
  ok "associated before test ($(wpa_cli -i "$WLAN" status | sed -n 's/^ssid=//p'))"
else
  echo "INFO: not associated before destroy (continuing)"
fi

# --- simulated boot ---
echo "--- destroy + recreate from rc ---"
elev pkill -f "dhclient.*${WLAN}" 2>/dev/null || true
elev pkill -f "wpa_supplicant.*${WLAN}" 2>/dev/null || true
sleep 1
if ifconfig "$WLAN" >/dev/null 2>&1; then
  elev ifconfig "$WLAN" destroy && ok "destroyed $WLAN" || bad "destroy failed"
else
  ok "$WLAN already absent"
fi
ifconfig "$WLAN" >/dev/null 2>&1 && bad "$WLAN still present" || ok "$WLAN gone"

elev ifconfig "$WLAN" create wlandev "$PARENT" && ok "created $WLAN wlandev $PARENT" || bad "create failed"
elev ifconfig "$WLAN" up || true
elev ifconfig "$WLAN" inet6 -ifdisabled 2>/dev/null || true
elev ifconfig "$WLAN" inet6 accept_rtadv 2>/dev/null || true
elev mkdir -p /var/run/wpa_supplicant
elev wpa_supplicant -B -i "$WLAN" -c "$CONF" -P "/var/run/wpa_supplicant/${WLAN}.pid" \
  && ok "wpa_supplicant started" || bad "wpa_supplicant failed"

# enable all saved nets (never select_network — that disables siblings)
wpa_cli -i "$WLAN" enable_network all >/dev/null 2>&1 || true
wpa_cli -i "$WLAN" reassociate >/dev/null 2>&1 || true

assoc=0
i=0
while [ "$i" -lt 35 ]; do
  i=$((i + 1))
  if wpa_cli -i "$WLAN" status 2>/dev/null | grep -q 'wpa_state=COMPLETED'; then
    assoc=1
    break
  fi
  sleep 1
done
if [ "$assoc" -eq 1 ]; then
  ok "associated after boot sim ($(wpa_cli -i "$WLAN" status | sed -n 's/^ssid=//p'))"
else
  bad "no association after boot sim"
  wpa_cli -i "$WLAN" status 2>&1 | head -15 || true
  wpa_cli -i "$WLAN" list_networks 2>&1 || true
fi

# Fresh DHCP (kill stale first)
elev sh -c "pidf=/var/run/dhclient.${WLAN}.pid; \
  if [ -f \"\$pidf\" ]; then kill \"\$(cat \"\$pidf\")\" 2>/dev/null; fi; \
  pkill -f \"dhclient.*${WLAN}\" 2>/dev/null; \
  rm -f \"\$pidf\"; dhclient ${WLAN}" || true
sleep 5
if ifconfig "$WLAN" 2>/dev/null | grep -q 'inet '; then
  ok "IPv4 $(ifconfig "$WLAN" | awk '/inet /{print $2; exit}')"
else
  bad "no IPv4 after DHCP restart"
fi

# --- persistence still there ---
[ "$(sysrc -n "wlans_${PARENT}" 2>/dev/null)" = "$WLANS" ] || \
  [ "$(sysrc -n "wlans_${PARENT}" 2>/dev/null)" = "$WLAN" ] && ok "rc wlans intact" || bad "rc wlans changed/lost"
echo "$(sysrc -n "ifconfig_${WLAN}" 2>/dev/null)" | grep -q WPA && ok "rc ifconfig intact" || bad "rc ifconfig lost"

# no SSID stack spam
ncount=$(wpa_cli -i "$WLAN" list_networks 2>/dev/null | awk 'NR>1 && NF' | wc -l | tr -d ' ')
echo "saved networks: $ncount"
[ "$ncount" -ge 1 ] && [ "$ncount" -le 8 ] && ok "network list size=$ncount" || bad "network list size=$ncount"

# aq0 must survive
ifconfig aq0 2>/dev/null | grep -q 'inet ' && ok "aq0 still addressed" || echo "INFO: no aq0 (ok if host has none)"

echo "--- final ---"
ifconfig "$WLAN" 2>/dev/null | head -12 || true
wpa_cli -i "$WLAN" status 2>/dev/null | grep -E 'ssid=|wpa_state=|ip_address=' || true
echo "rc:"; grep -E "wlans_|ifconfig_${WLAN}|ifconfig_aq0" /etc/rc.conf 2>/dev/null || true
wpa_cli -i "$WLAN" list_networks 2>/dev/null || true

echo "======== SUMMARY pass=$pass fail=$fail ========"
[ "$fail" -eq 0 ]
