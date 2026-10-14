/* Host executable exercises the exact header used by the firmware. */
#include <assert.h>
#include <stdio.h>
#include "../csi_node/main/node_protocol.h"

static void schedules(void)
{
    node_config_t c = {.id = 1, .mode = 2, .rate_hz = 40, .phase_ms = 5000};
    assert(node_config_valid(&c));
    assert(node_slot_us(&c) == 12500);
    unsigned counts[4] = {0};
    for (uint32_t slot = 0; slot < 1600; slot++) {
        node_packet_t p = node_make_grant(&c, 123, slot);
        assert(node_packet_valid(&p, sizeof(p), &c, 123));
        assert(p.phase == slot / 400);
        assert(p.tx_id == (slot / 400 % 2 ? 3 : 1) + slot % 2);
        assert(p.rx_mask == (slot / 400 % 2 ? 3 : 12));
        assert(!(p.rx_mask & (1u << (p.tx_id - 1))));
        counts[p.tx_id - 1]++;
    }
    for (unsigned id = 0; id < 4; id++) assert(counts[id] == 400);
    c.mode = 1;
    assert(node_slot_us(&c) == 25000);
    for (uint32_t slot = 0; slot < 500; slot++) {
        node_packet_t p = node_make_grant(&c, 123, slot);
        assert(p.tx_id == 1 && p.phase == 0 && p.rx_mask == 14);
    }
    c.mode = 2; c.rate_hz = 60; assert(!node_config_valid(&c));
    c.rate_hz = 39; assert(!node_config_valid(&c));
    c.rate_hz = 40; c.phase_ms = 1001; assert(!node_config_valid(&c));
    c.phase_ms = 1025; assert(node_config_valid(&c));
    c.id = 0; assert(!node_config_valid(&c));
}

static void grants_and_sessions(void)
{
    node_config_t c = {.id = 3, .mode = 2, .rate_hz = 40, .phase_ms = 5000};
    node_packet_t p = node_make_grant(&c, 123, 400);
    assert(p.tx_id == 3 && p.phase == 1);
    assert(!node_can_respond(&p, 3, -1, false, false));
    assert(!node_can_respond(&p, 3, 999, false, false));
    assert(node_can_respond(&p, 3, 1000, false, false));
    assert(node_can_respond(&p, 3, 2500, false, false));
    assert(!node_can_respond(&p, 3, 2501, false, false));
    assert(!node_can_respond(&p, 3, 1000000, false, false));
    assert(!node_can_respond(&p, 1, 1000, false, false));
    assert(!node_can_respond(&p, 3, 1000, true, false));
    assert(!node_can_respond(&p, 3, 1000, false, true));
    assert(node_radio_age_us(1000, 900) == 100);
    assert(node_radio_age_us(200, UINT32_MAX - 99) == 300);
    assert(node_radio_age_us(900, 1000) > NODE_ENQUEUE_DEADLINE_US);
    assert(!node_packet_valid(&p, sizeof(p), &c, 0));
    assert(!node_packet_valid(&p, sizeof(p), &c, 124));
    assert(!node_packet_valid(&p, sizeof(p) - 1, &c, 123));
    node_packet_t stale = node_make_grant(&c, 123, 399);
    assert(!node_same_slot(&stale, &p));
    node_packet_t data = p; data.type = NODE_DATA;
    assert(node_same_slot(&data, &p));
    assert(!node_can_respond(&data, 3, 1000, false, false));
    data.rx_mask = 15; assert(!node_packet_valid(&data, sizeof(data), &c, 123));
    data = p; data.tx_id = 4; assert(!node_packet_valid(&data, sizeof(data), &c, 123));
    data = p; data.phase = 0; assert(!node_packet_valid(&data, sizeof(data), &c, 123));
    data = p; data.seq++; assert(!node_packet_valid(&data, sizeof(data), &c, 123));
    data = p; data.phase_ms = 10000; assert(!node_packet_valid(&data, sizeof(data), &c, 123));
    for (uint8_t id = 1; id <= 4; id++) {
        uint8_t mac[6]; node_mac(id, mac);
        assert(node_source_valid(id, mac));
        assert(!node_source_valid(id % 4 + 1, mac));
        assert((mac[0] & 3) == 2);
    }
}

static void bounded_parser(void)
{
    node_config_t c = {.id = 2, .mode = 1, .rate_hz = 40, .phase_ms = 5000};
    node_packet_t p = node_make_grant(&c, 987, 123), decoded;
    uint8_t body[15 + sizeof(p)] = {127,0x18,0xfe,0x34,0,0,0,0,221,
                                   5 + sizeof(p),0x18,0xfe,0x34,4,2};
    memcpy(body + 15, &p, sizeof(p));
    assert(!node_parse_action(NULL, sizeof(body), &decoded));
    for (size_t len = 0; len < sizeof(body); len++) assert(!node_parse_action(body, len, &decoded));
    assert(node_parse_action(body, sizeof(body), &decoded));
    assert(node_same_slot(&p, &decoded));
    body[14] = 1; assert(node_parse_action(body, sizeof(body), &decoded));
    body[14] = 0x12; assert(!node_parse_action(body, sizeof(body), &decoded));
    body[14] = 2;
    const unsigned checked[] = {0,1,2,3,8,9,10,11,12,13,14,15,19,21};
    for (unsigned i = 0; i < sizeof(checked)/sizeof(checked[0]); i++) {
        unsigned offset = checked[i]; uint8_t old = body[offset];
        body[offset] = 255;
        assert(!node_parse_action(body, sizeof(body), &decoded));
        body[offset] = old;
    }
    p.type = NODE_DATA; memcpy(body + 15, &p, sizeof(p));
    assert(node_parse_action(body, sizeof(body), &decoded));
    assert(decoded.type == NODE_DATA);
}

static void base64(void)
{
    int8_t values[] = {0, 1, 127, -128, -1};
    char out[NODE_BASE64_CAPACITY];
    assert(node_csi_base64(values, 5, out, sizeof(out)) && !strcmp(out, "AAF/gP8="));
    assert(node_csi_base64(values, 3, out, sizeof(out)) && !strcmp(out, "AAF/"));
    assert(node_csi_base64(values, 1, out, sizeof(out)) && !strcmp(out, "AA=="));
    assert(!node_csi_base64(values, 5, out, 8));
    int8_t maximum[NODE_MAX_CSI] = {0};
    assert(node_csi_base64(maximum, sizeof(maximum), out, sizeof(out)));
    assert(strlen(out) == 1024);
    assert(!node_csi_base64(maximum, sizeof(maximum) + 1, out, sizeof(out)));
}

static void late_radio_guard(void)
{
    assert(NODE_POST_GRANT_QUIET_US == 11000);
    assert(NODE_RX_LEASE_US >= NODE_ENQUEUE_DEADLINE_US + NODE_DATA_WATCHDOG_US);
    assert(node_send_watchdog_us(NODE_GRANT) == 100000);
    assert(node_send_watchdog_us(NODE_DATA) == 8000);
    /* 12.5 ms nominal slot: ordinary grant callback leaves cadence unchanged. */
    assert(node_next_grant_us(12500, 400) == 12500);
    assert(node_next_grant_us(12500, 1500) == 12500);
    /* Radio contention extends the slot instead of truncating slave airtime. */
    assert(node_next_grant_us(12500, 1501) == 12501);
    assert(node_next_grant_us(12500, 5000) == 16000);
    assert(node_next_grant_us(12500, 8000) == 19000);
    assert(node_next_grant_us(12500, 9000) == 20000);
    assert(node_next_grant_us(12500, 100000) == 111000);
    assert(node_next_grant_us(25000, 8000) == 25000);
    /* Later nominal deadlines are never pulled backward or burst to catch up. */
    assert(node_next_grant_us(16000 + 12500, 16400) == 28500);
    for (int64_t completion = 0; completion <= 8000; completion++) {
        int64_t next = node_next_grant_us(12500, completion);
        assert(next >= 12500);
        assert(next - completion >= NODE_ENQUEUE_DEADLINE_US + NODE_DATA_WATCHDOG_US + NODE_SLOT_GUARD_US);
    }
}

int main(void)
{
    schedules(); grants_and_sessions(); bounded_parser(); base64(); late_radio_guard();
    puts("node_protocol: schedule/phase, grant expiry, wrong-session/source, parser bounds passed");
    return 0;
}
