#include "cli.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "access_control.h"
#include "auth.h"
#include "config.h"
#include "door.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "health.h"
#include "light.h"
#include "logger.h"
#include "net_protocol.h"
#include "net_wifi.h"
#include "occupancy_ledger.h"
#include "pir_sensor.h"
#include "rfid.h"
#include "schedule.h"
#include "storage_sd.h"
#include "sync.h"
#include "time_sync.h"
#include "user_db.h"

static const char *TAG = "CLI";

static void print_prompt(void)
{
    printf("\n%s", auth_is_logged_in() ? "cocis(admin)# " : "cocis> ");
    fflush(stdout);
}

static bool require_admin(void)
{
    if (!auth_is_logged_in()) {
        printf("ERROR: Admin authentication required. Run: login <user> <pass>\n");
        logger_log_audit("ANONYMOUS", "UNAUTH_CMD_BLOCKED", "Attempted privileged CLI action");
        return false;
    }
    auth_touch_activity();
    return true;
}

static void cmd_help(void)
{
    printf("\n==============================================================================\n");
    printf("              CoCIS SMART CONTROL & MONITORING CLI REFERENCE\n");
    printf("==============================================================================\n");
    printf(" [MONITORING COMMANDS]\n");
    printf("  status                             System dashboard (Clock, Doors, Rooms, SD)\n");
    printf("  inside | occupancy                 List everyone currently inside CoCIS\n");
    printf("  rooms  | lights                    Lecture Room 1 & 2 PIR, lights & energy\n");
    printf("  log [n]                            Show last n (default 15) hash-chained logs\n");
    printf("  log user <uid>                     Filter access/audit logs for RFID <uid>\n");
    printf("  stats                              Access grant/deny & anti-passback metrics\n");
    printf("  nodes                              Distributed ESP32 terminal health & queues\n");
    printf("  daily_report                       Generate management daily summary report\n");
    printf("\n [ADMINISTRATION COMMANDS (Login Required: default admin / Cocis@2026)]\n");
    printf("  login <user> <pass> [2fa_uid]      Authenticate admin session\n");
    printf("  logout                             Close current admin session\n");
    printf("  passwd <old_pass> <new_pass>       Update salted SHA-256 admin password\n");
    printf("  schedule show                      Display weekly authorized access schedule\n");
    printf("  schedule set <day|all> <HH:MM> <HH:MM>  Set open/close hours (e.g. mon 07:00 21:00)\n");
    printf("  schedule close <day>               Close building for an entire day of week\n");
    printf("  override_exit <uid>                Grant 5-min supervisor safe-exit pass\n");
    printf("  emergency <on|off>                 Activate/clear fire evacuation unlock mode\n");
    printf("  user list                          List all enrolled RFID users and roles\n");
    printf("  user add <uid> <role> <name>       Enroll RFID tag (admin|staff|student|visitor)\n");
    printf("  user del <uid>                     Delete enrolled RFID tag\n");
    printf("  user enable|disable <uid>          Enable or suspend an enrolled RFID tag\n");
    printf("  sd status | verify | format        Inspect, verify SHA-256 chain, or format SD\n");
    printf("  time [set YYYY-MM-DD HH:MM:SS]     Show or configure East Africa Time (EAT)\n");
    printf("\n [SIMULATION & VERIFICATION HELPERS]\n");
    printf("  sim scan <entry|exit> <uid>        Simulate RFID card scan at Entry or Exit\n");
    printf("  sim motion <1|2> <on|off>          Simulate PIR motion in Lecture Room 1 or 2\n");
    printf("  sim node <1|2> <online|offline>    Simulate network link drop & store-forward\n");
    printf("  sim door_hold <entry|exit>         Simulate forced/propped door alarm\n");
    printf("  sim selftest                       Run automated 10-scenario coursework suite\n");
    printf("==============================================================================\n");
}

static void cmd_status(void)
{
    time_snapshot_t now;
    time_sync_get_now(&now);
    char iso[32];
    time_sync_format_iso(now.epoch, iso, sizeof(iso));

    weekly_schedule_t sched;
    schedule_get_weekly(&sched);
    const day_schedule_t *today = &sched.days[now.day_of_week];

    occupancy_stats_t ostats;
    occupancy_ledger_get_stats(&ostats);

    door_status_t d_in, d_out;
    door_get_status(DOOR_ID_ENTRY, &d_in);
    door_get_status(DOOR_ID_EXIT, &d_out);

    room_light_status_t r1, r2;
    light_get_room_status(1, &r1);
    light_get_room_status(2, &r2);

    sd_status_t sdst;
    storage_sd_get_status(&sdst);
    char tip[16];
    logger_get_chain_tip(tip, sizeof(tip));

    wifi_status_t wst;
    net_wifi_get_status(&wst);

    health_summary_t hst;
    health_get_summary(&hst);

    auth_status_t ast;
    auth_get_status(&ast);

    printf("\n==============================================================================\n");
    printf("             CoCIS SMART CONTROL & MONITORING SYSTEM - STATUS\n");
    printf("==============================================================================\n");
    printf(" System Clock (EAT) : %s (%s) [%s]\n",
           iso, schedule_day_name(now.day_of_week),
           now.time_valid ? (now.sntp_synced ? "VALID RTC+SNTP" : "VALID RTC") : "INVALID");
    if (today->enabled) {
        printf(" Building Schedule  : %s (Today's Window: %02u:%02u - %02u:%02u EAT)\n",
               schedule_is_open_now() ? "OPEN" : "CLOSED (After Hours)",
               today->open_hour, today->open_min, today->close_hour, today->close_min);
    } else {
        printf(" Building Schedule  : CLOSED ALL DAY (%s)\n", schedule_day_name(now.day_of_week));
    }
    printf(" Emergency Mode     : %s\n",
           schedule_is_emergency_mode() ? "!! ACTIVE - ALL DOORS UNLOCKED FOR EVACUATION !!" : "NORMAL OPERATION");
    printf(" Live Occupancy     : %lu person(s) inside (Peak: %lu | Entries: %lu | Exits: %lu)\n",
           (unsigned long)ostats.current_inside, (unsigned long)ostats.peak_inside,
           (unsigned long)ostats.total_entries, (unsigned long)ostats.total_exits);
    printf(" Main Entrance Doors: Entry=%s | Exit=%s\n",
           door_state_to_str(d_in.state), door_state_to_str(d_out.state));
    printf(" Lecture Room 1     : PIR=%-6s | Light=%-10s | Saved=%lu mWh\n",
           r1.pir_motion_active ? "MOTION" : "IDLE", light_fsm_to_str(r1.fsm_state),
           (unsigned long)r1.energy_saved_mwh);
    printf(" Lecture Room 2     : PIR=%-6s | Light=%-10s | Saved=%lu mWh\n",
           r2.pir_motion_active ? "MOTION" : "IDLE", light_fsm_to_str(r2.fsm_state),
           (unsigned long)r2.energy_saved_mwh);
    printf(" SD Card Storage    : %s | Chain Tip=%s | Writes=%lu\n",
           sdst.mounted ? "MOUNTED (/sdcard)" : "RAM FALLBACK", tip, (unsigned long)sdst.write_ok_count);
    printf(" Distributed Nodes  : %u/3 Nodes ONLINE | Wi-Fi=%s (%s) | MQTT=%s\n",
           (unsigned)hst.online_nodes,
           wst.connected ? "CONNECTED" : "DISCONNECTED", wst.ip_addr,
           net_protocol_is_mqtt_connected() ? "ONLINE" : "LOCAL_BUS");
    printf(" Admin CLI Session  : %s (Free Heap: %lu B, Uptime: %lu s)\n",
           ast.logged_in ? "AUTHENTICATED" : "LOCKED (Guest)",
           (unsigned long)hst.free_heap_bytes, (unsigned long)hst.uptime_sec);
    printf("==============================================================================\n");
}

static void cmd_inside(void)
{
    occupant_record_t list[CFG_USER_DB_MAX_USERS];
    size_t count = occupancy_ledger_list_inside(list, CFG_USER_DB_MAX_USERS);
    time_snapshot_t now;
    time_sync_get_now(&now);

    printf("\n+------+------------+--------------------------+---------+---------------------+----------+\n");
    printf("| Slot | Tag UID    | Full Name                | Role    | Entry Time (EAT)    | Duration |\n");
    printf("+------+------------+--------------------------+---------+---------------------+----------+\n");
    for (size_t i = 0; i < count; i++) {
        char entry_iso[32];
        time_sync_format_iso(list[i].entry_epoch, entry_iso, sizeof(entry_iso));
        int64_t dur = (now.epoch >= list[i].entry_epoch) ? (now.epoch - list[i].entry_epoch) : 0;
        int hh = (int)(dur / 3600);
        int mm = (int)((dur % 3600) / 60);
        int ss = (int)(dur % 60);
        printf("| %02u   | %-10s | %-24.24s | %-7s | %-19s | %02d:%02d:%02d |\n",
               (unsigned)(i + 1),
               list[i].uid,
               list[i].name,
               user_db_role_to_str(list[i].role),
               entry_iso,
               hh, mm, ss);
    }
    if (count == 0) {
        printf("| --   | (Building is currently unoccupied - 0 persons inside CoCIS)                  |\n");
    }
    printf("+------+------------+--------------------------+---------+---------------------+----------+\n");
    printf("Total Currently Inside CoCIS: %u person(s)\n", (unsigned)count);
}

static void cmd_rooms(void)
{
    printf("\n+--------+----------+------------+------------+-----------+------------+-------------+--------------+\n");
    printf("| Room   | Node St. | PIR Sensor | Light FSM  | Relay Pin | Time Left  | Light On(s) | Energy Saved |\n");
    printf("+--------+----------+------------+------------+-----------+------------+-------------+--------------+\n");

    uint32_t total_saved = 0;
    for (uint8_t r = 1; r <= CFG_ROOM_COUNT; r++) {
        room_light_status_t st;
        light_get_room_status(r, &st);
        node_net_status_t nst;
        net_protocol_get_node_status((r == 1) ? NODE_ID_ROOM_1 : NODE_ID_ROOM_2, &nst);
        total_saved += st.energy_saved_mwh;

        printf("| Room %u | %-8s | %-10s | %-10s | %-9s | %8lu s | %9lu s | %8lu mWh |\n",
               (unsigned)r,
               nst.link_simulated_online ? "ONLINE" : "OFFLINE",
               st.pir_motion_active ? "MOTION" : "VACANT",
               light_fsm_to_str(st.fsm_state),
               st.relay_on ? "HIGH (ON)" : "LOW (OFF)",
               (unsigned long)st.seconds_until_off,
               (unsigned long)st.total_light_on_sec,
               (unsigned long)st.energy_saved_mwh);
    }
    printf("+--------+----------+------------+------------+-----------+------------+-------------+--------------+\n");
    uint32_t ugx_equiv = (total_saved * CFG_UGX_PER_KWH) / 1000U;
    printf("Cumulative Building Energy Saved: %lu mWh (Scaled Pilot Cost Reductions: ~%lu UGX)\n",
           (unsigned long)total_saved, (unsigned long)ugx_equiv);
}

static void cmd_log(int argc, char **argv)
{
    size_t limit = 15;
    const char *filter_uid = NULL;

    if (argc >= 2) {
        if (strcasecmp(argv[1], "user") == 0 && argc >= 3) {
            filter_uid = argv[2];
            limit = 32;
        } else {
            int val = atoi(argv[1]);
            if (val > 0 && val <= CFG_LOG_RAM_RING_CAP) {
                limit = (size_t)val;
            }
        }
    }

    logger_flush_sync(100);
    log_entry_t entries[CFG_LOG_RAM_RING_CAP];
    size_t count = logger_get_recent(entries, limit, filter_uid);

    printf("\n+------+---------------------+------------+----------------------+--------------------------+----------+----------+\n");
    printf("| Seq  | Timestamp (EAT)     | UID/Actor  | Action               | Detail                   | Val/Cnt  | SHA256   |\n");
    printf("+------+---------------------+------------+----------------------+--------------------------+----------+----------+\n");
    for (size_t i = 0; i < count; i++) {
        char ts[32];
        time_sync_format_iso(entries[i].timestamp, ts, sizeof(ts));
        printf("| %04lu | %-19s | %-10.10s | %-20.20s | %-24.24s | %8ld | %-8s |\n",
               (unsigned long)entries[i].seq_id,
               ts,
               entries[i].tag_or_actor,
               entries[i].action,
               entries[i].detail,
               (long)entries[i].value,
               entries[i].chain_hash);
    }
    if (count == 0) {
        printf("| ---- | No matching log records found                                                                        |\n");
    }
    printf("+------+---------------------+------------+----------------------+--------------------------+----------+----------+\n");
}

static void cmd_stats(void)
{
    access_stats_t astats;
    access_control_get_stats(&astats);
    occupancy_stats_t ostats;
    occupancy_ledger_get_stats(&ostats);

    printf("\n+----------------------------------------+------------+\n");
    printf("| Security & Performance Metric          |      Value |\n");
    printf("+----------------------------------------+------------+\n");
    printf("| Granted Entries                        | %10lu |\n", (unsigned long)astats.granted_entry);
    printf("| Granted Exits                          | %10lu |\n", (unsigned long)astats.granted_exit);
    printf("| Denied: Unknown RFID Tag               | %10lu |\n", (unsigned long)astats.denied_unknown);
    printf("| Denied: Inactive / Suspended User      | %10lu |\n", (unsigned long)astats.denied_inactive);
    printf("| Denied: Expired Visitor Badge          | %10lu |\n", (unsigned long)astats.denied_expired);
    printf("| Denied: Outside Schedule (Entry)       | %10lu |\n", (unsigned long)astats.denied_schedule_entry);
    printf("| Denied: Outside Schedule (Exit)        | %10lu |\n", (unsigned long)astats.denied_schedule_exit);
    printf("| Denied: Anti-Passback Violations       | %10lu |\n", (unsigned long)astats.denied_antipassback);
    printf("| Current Building Occupancy             | %10lu |\n", (unsigned long)ostats.current_inside);
    printf("| Peak Building Occupancy                | %10lu |\n", (unsigned long)ostats.peak_inside);
    printf("| Boot-Recovered Occupants from SD       | %10lu |\n", (unsigned long)ostats.recovered_on_boot);
    printf("| Total Energy Saved (mWh)               | %10lu |\n", (unsigned long)light_get_total_energy_saved_mwh());
    printf("+----------------------------------------+------------+\n");

    printf("\n[Hierarchical Mutex Lock Diagnostics]\n");
    printf("+----+---------------+--------------+--------------+\n");
    printf("| Lv | Lock Name     | Acquires     | Timeouts     |\n");
    printf("+----+---------------+--------------+--------------+\n");
    for (int lv = LOCK_LEVEL_CONFIG; lv < LOCK_LEVEL_COUNT; lv++) {
        sync_lock_stats_t lst;
        if (sync_get_stats((sync_lock_level_t)lv, &lst) == ESP_OK) {
            printf("| %2d | %-13s | %12lu | %12lu |\n",
                   lv, lst.name, (unsigned long)lst.acquire_count, (unsigned long)lst.timeout_count);
        }
    }
    printf("+----+---------------+--------------+--------------+\n");
}

static void cmd_nodes(void)
{
    printf("\n+----+-----------------------+---------+--------+--------+--------+--------+----------+----------+\n");
    printf("| ID | Terminal Name         | Link    | TxSeq  | RxSeq  | ACKed  | DupSup | Buffered | Replayed |\n");
    printf("+----+-----------------------+---------+--------+--------+--------+--------+----------+----------+\n");
    for (int i = 0; i < NODE_ID_COUNT; i++) {
        node_net_status_t st;
        if (net_protocol_get_node_status((node_id_t)i, &st) == ESP_OK) {
            printf("| %2d | %-21.21s | %-7s | %6lu | %6lu | %6lu | %6lu | %8lu | %8lu |\n",
                   i,
                   st.name,
                   st.link_simulated_online ? "ONLINE" : "OFFLINE",
                   (unsigned long)st.last_tx_seq,
                   (unsigned long)st.last_rx_seq,
                   (unsigned long)st.acked_count,
                   (unsigned long)st.dup_suppressed_count,
                   (unsigned long)st.buffered_offline_count,
                   (unsigned long)st.replayed_count);
        }
    }
    printf("+----+-----------------------+---------+--------+--------+--------+--------+----------+----------+\n");
}

static void cmd_daily_report(void)
{
    printf("\n******************************************************************************\n");
    printf("*        MAKERERE UNIVERSITY - CoCIS DAILY EXECUTIVE SECURITY REPORT         *\n");
    printf("******************************************************************************\n");
    cmd_status();
    cmd_inside();
    cmd_rooms();
    cmd_stats();
}

static void cmd_schedule(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "show") == 0) {
        weekly_schedule_t sched;
        schedule_get_weekly(&sched);
        printf("\n+-----+-----------+---------------+---------------+------------------+\n");
        printf("| Day | Status    | Open Time EAT | Close Time    | Policy           |\n");
        printf("+-----+-----------+---------------+---------------+------------------+\n");
        for (int d = 0; d < 7; d++) {
            const day_schedule_t *ds = &sched.days[d];
            printf("| %-3s | %-9s |     %02u:%02u     |     %02u:%02u     | %-16s |\n",
                   schedule_day_name(d),
                   ds->enabled ? "ENABLED" : "CLOSED",
                   ds->open_hour, ds->open_min,
                   ds->close_hour, ds->close_min,
                   ds->enabled ? "Entry/Exit Enfc." : "Admin/Evac Only");
        }
        printf("+-----+-----------+---------------+---------------+------------------+\n");
        return;
    }

    if (!require_admin()) {
        return;
    }

    if (strcasecmp(argv[1], "close") == 0 && argc >= 3) {
        int d = schedule_parse_day_name(argv[2]);
        if (d < 0) {
            printf("ERROR: Invalid day '%s'. Use sun, mon, tue, wed, thu, fri, sat.\n", argv[2]);
            return;
        }
        schedule_set_day(d, false, 0, 0, 0, 0);
        net_protocol_broadcast_schedule_sync();
        logger_log_audit("admin", "SCHEDULE_CLOSE", schedule_day_name(d));
        printf("OK: Marked %s as CLOSED all day.\n", schedule_day_name(d));
        return;
    }

    if (strcasecmp(argv[1], "set") == 0 && argc >= 5) {
        int oh = 0, om = 0, ch = 0, cm = 0;
        if (sscanf(argv[3], "%d:%d", &oh, &om) != 2 || sscanf(argv[4], "%d:%d", &ch, &cm) != 2) {
            printf("ERROR: Time format must be HH:MM (e.g. schedule set mon 07:00 21:00)\n");
            return;
        }

        if (strcasecmp(argv[2], "all") == 0) {
            for (int d = 0; d < 7; d++) {
                if (schedule_set_day(d, true, (uint8_t)oh, (uint8_t)om, (uint8_t)ch, (uint8_t)cm) != ESP_OK) {
                    printf("ERROR: Invalid time window %s - %s\n", argv[3], argv[4]);
                    return;
                }
            }
            net_protocol_broadcast_schedule_sync();
            logger_log_audit("admin", "SCHEDULE_SET_ALL", argv[3]);
            printf("OK: Updated ALL days to %02d:%02d - %02d:%02d EAT.\n", oh, om, ch, cm);
            return;
        }

        int d = schedule_parse_day_name(argv[2]);
        if (d < 0) {
            printf("ERROR: Unknown day '%s'. Use sun..sat or all.\n", argv[2]);
            return;
        }
        if (schedule_set_day(d, true, (uint8_t)oh, (uint8_t)om, (uint8_t)ch, (uint8_t)cm) != ESP_OK) {
            printf("ERROR: Close time must be strictly after open time.\n");
            return;
        }
        net_protocol_broadcast_schedule_sync();
        char detail[48];
        snprintf(detail, sizeof(detail), "%s %02d:%02d-%02d:%02d", schedule_day_name(d), oh, om, ch, cm);
        logger_log_audit("admin", "SCHEDULE_SET", detail);
        printf("OK: Updated %s schedule to %02d:%02d - %02d:%02d EAT.\n", schedule_day_name(d), oh, om, ch, cm);
        return;
    }

    printf("Usage: schedule show | schedule set <day|all> <HH:MM> <HH:MM> | schedule close <day>\n");
}

static void cmd_user(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "list") == 0) {
        user_record_t users[CFG_USER_DB_MAX_USERS];
        size_t n = user_db_list(users, CFG_USER_DB_MAX_USERS);
        printf("\n+------------+--------------------------+---------+--------+----------+\n");
        printf("| Tag UID    | Full Name                | Role    | Active | Location |\n");
        printf("+------------+--------------------------+---------+--------+----------+\n");
        for (size_t i = 0; i < n; i++) {
            bool inside = occupancy_ledger_is_inside(users[i].uid);
            printf("| %-10s | %-24.24s | %-7s | %-6s | %-8s |\n",
                   users[i].uid,
                   users[i].name,
                   user_db_role_to_str(users[i].role),
                   users[i].active ? "YES" : "NO",
                   inside ? "INSIDE" : "OUTSIDE");
        }
        printf("+------------+--------------------------+---------+--------+----------+\n");
        printf("Total Enrolled Users: %u / %d\n", (unsigned)n, CFG_USER_DB_MAX_USERS);
        return;
    }

    if (!require_admin()) {
        return;
    }

    if (strcasecmp(argv[1], "add") == 0 && argc >= 5) {
        user_role_t role;
        if (!user_db_parse_role(argv[3], &role)) {
            printf("ERROR: Invalid role '%s'. Use admin, staff, student, or visitor.\n", argv[3]);
            return;
        }
        char full_name[CFG_USER_NAME_MAX_LEN] = { 0 };
        for (int i = 4; i < argc; i++) {
            if (i > 4) {
                strncat(full_name, " ", sizeof(full_name) - strlen(full_name) - 1);
            }
            strncat(full_name, argv[i], sizeof(full_name) - strlen(full_name) - 1);
        }
        int64_t expiry = 0;
        if (role == USER_ROLE_VISITOR) {
            time_snapshot_t now;
            time_sync_get_now(&now);
            expiry = now.epoch + CFG_VISITOR_DEFAULT_TTL_SEC;
        }
        if (user_db_add(argv[2], full_name, role, true, expiry) == ESP_OK) {
            logger_log_audit("admin", "USER_ADD", argv[2]);
            printf("OK: Enrolled %s (%s) with role %s.\n", argv[2], full_name, user_db_role_to_str(role));
        } else {
            printf("ERROR: Failed to enroll user (database full or invalid UID).\n");
        }
        return;
    }

    if (strcasecmp(argv[1], "del") == 0 && argc >= 3) {
        if (user_db_remove(argv[2]) == ESP_OK) {
            logger_log_audit("admin", "USER_DELETE", argv[2]);
            printf("OK: Removed user %s.\n", argv[2]);
        } else {
            printf("ERROR: UID %s not found.\n", argv[2]);
        }
        return;
    }

    if ((strcasecmp(argv[1], "enable") == 0 || strcasecmp(argv[1], "disable") == 0) && argc >= 3) {
        bool en = (strcasecmp(argv[1], "enable") == 0);
        if (user_db_set_active(argv[2], en) == ESP_OK) {
            logger_log_audit("admin", en ? "USER_ENABLE" : "USER_DISABLE", argv[2]);
            printf("OK: User %s is now %s.\n", argv[2], en ? "ACTIVE" : "DISABLED");
        } else {
            printf("ERROR: UID %s not found.\n", argv[2]);
        }
        return;
    }

    printf("Usage: user list | user add <uid> <role> <name> | user del <uid> | user enable|disable <uid>\n");
}

static void cmd_sd(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "status") == 0) {
        sd_status_t st;
        storage_sd_get_status(&st);
        char tip[16];
        logger_get_chain_tip(tip, sizeof(tip));
        printf("\n[SD Card & Hash-Chain Storage Status]\n");
        printf("  Mounted        : %s\n", st.mounted ? "YES (/sdcard via SPI2 SDSPI)" : "NO (RAM Ring-Buffer Active)");
        printf("  access.csv     : %lu bytes\n", (unsigned long)st.access_log_bytes);
        printf("  lighting.csv   : %lu bytes\n", (unsigned long)st.lighting_log_bytes);
        printf("  audit.csv      : %lu bytes\n", (unsigned long)st.audit_log_bytes);
        printf("  Write Count    : %lu OK / %lu Errors\n",
               (unsigned long)st.write_ok_count, (unsigned long)st.write_err_count);
        printf("  SHA-256 Tip    : %s\n", tip);
        return;
    }

    if (strcasecmp(argv[1], "verify") == 0) {
        uint32_t checked = 0;
        bool valid = false;
        logger_verify_integrity(&checked, &valid);
        char tip[16];
        logger_get_chain_tip(tip, sizeof(tip));
        printf("SHA-256 Log Chain Verification: Checked %lu record(s) -> %s (Tip=%s)\n",
               (unsigned long)checked, valid ? "INTEGRITY OK (UNTAMPERED)" : "TAMPER DETECTED!", tip);
        return;
    }

    if (strcasecmp(argv[1], "format") == 0) {
        if (!require_admin()) {
            return;
        }
        logger_reset_chain();
        logger_log_audit("admin", "SD_FORMAT", "Cleared logs and reset genesis hash chain");
        printf("OK: SD CSV logs formatted and SHA-256 chain reset to genesis.\n");
        return;
    }

    printf("Usage: sd status | sd verify | sd format\n");
}

static void cmd_time(int argc, char **argv)
{
    if (argc >= 4 && strcasecmp(argv[1], "set") == 0) {
        if (!require_admin()) {
            return;
        }
        int yr = 0, mo = 0, dy = 0, hh = 0, mm = 0, ss = 0;
        if (sscanf(argv[2], "%d-%d-%d", &yr, &mo, &dy) != 3 ||
            sscanf(argv[3], "%d:%d:%d", &hh, &mm, &ss) != 3) {
            printf("Usage: time set YYYY-MM-DD HH:MM:SS\n");
            return;
        }
        if (time_sync_set_datetime(yr, mo, dy, hh, mm, ss) == ESP_OK) {
            logger_log_audit("admin", "TIME_SET", argv[2]);
            printf("OK: System clock updated to %04d-%02d-%02d %02d:%02d:%02d EAT.\n",
                   yr, mo, dy, hh, mm, ss);
        } else {
            printf("ERROR: Invalid date/time parameters.\n");
        }
        return;
    }

    time_snapshot_t now;
    time_sync_get_now(&now);
    char iso[32];
    time_sync_format_iso(now.epoch, iso, sizeof(iso));
    printf("Current System Clock: %s EAT (%s, Epoch=%lld, Valid=%s)\n",
           iso, schedule_day_name(now.day_of_week), (long long)now.epoch,
           now.time_valid ? "YES" : "NO");
}

static void run_selftest_suite(void)
{
    printf("\n==============================================================================\n");
    printf("       RUNNING CoCIS AUTOMATED 10-SCENARIO COURSEWORK VERIFICATION SUITE\n");
    printf("==============================================================================\n");

    int passed = 0;
    int total = 10;

    /* Reset baseline state for deterministic self-test */
    schedule_set_emergency_mode(false);
    schedule_reset_defaults();
    occupancy_ledger_clear_all();
    auth_reset_lockout();
    auth_logout();
    time_sync_set_datetime(2026, 10, 24, 9, 0, 0); /* Saturday 09:00:00 EAT (Open: 08:00-17:00) */

    /* Test 1: Valid Entry */
    access_result_t r1 = access_control_evaluate_sync(RFID_READER_ENTRY, "04A1B2C3");
    bool t1 = (r1 == ACCESS_RESULT_GRANTED_NORMAL) && (occupancy_ledger_get_count() == 1);
    printf(" [Test 01] Valid RFID Entry & Occupancy Increment      : %s\n", t1 ? "PASS" : "FAIL");
    if (t1) passed++;

    /* Test 2: Anti-Passback on Duplicate Entry */
    access_result_t r2 = access_control_evaluate_sync(RFID_READER_ENTRY, "04A1B2C3");
    bool t2 = (r2 == ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY) && (occupancy_ledger_get_count() == 1);
    printf(" [Test 02] Anti-Passback Violation (Duplicate Entry)   : %s\n", t2 ? "PASS" : "FAIL");
    if (t2) passed++;

    /* Test 3: Valid Exit & Occupancy Decrement */
    access_result_t r3 = access_control_evaluate_sync(RFID_READER_EXIT, "04A1B2C3");
    bool t3 = (r3 == ACCESS_RESULT_GRANTED_NORMAL) && (occupancy_ledger_get_count() == 0);
    printf(" [Test 03] Valid RFID Exit & Occupancy Decrement       : %s\n", t3 ? "PASS" : "FAIL");
    if (t3) passed++;

    /* Test 4: Anti-Passback on Exit When Outside */
    access_result_t r4 = access_control_evaluate_sync(RFID_READER_EXIT, "04A1B2C3");
    bool t4 = (r4 == ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT);
    printf(" [Test 04] Anti-Passback Violation (Exit When Outside) : %s\n", t4 ? "PASS" : "FAIL");
    if (t4) passed++;

    /* Test 5: Unknown Tag & Inactive User Denial */
    access_result_t r5a = access_control_evaluate_sync(RFID_READER_ENTRY, "DEADBEEF");
    access_result_t r5b = access_control_evaluate_sync(RFID_READER_ENTRY, "0499DEAD");
    bool t5 = (r5a == ACCESS_RESULT_DENIED_UNKNOWN_TAG) && (r5b == ACCESS_RESULT_DENIED_USER_INACTIVE);
    printf(" [Test 05] Unknown Tag & Inactive User Rejection       : %s\n", t5 ? "PASS" : "FAIL");
    if (t5) passed++;

    /* Test 6: Schedule Enforcement (After-Hours Entry & Exit Denial + Supervisor Override) */
    access_control_evaluate_sync(RFID_READER_ENTRY, "04C3D4E5"); /* Sarah enters at 09:00 */
    time_sync_set_datetime(2026, 10, 24, 22, 0, 0); /* 22:00 EAT (After 17:00 Sat close) */
    access_result_t r6_in = access_control_evaluate_sync(RFID_READER_ENTRY, "04D4E5F6");
    access_result_t r6_out_deny = access_control_evaluate_sync(RFID_READER_EXIT, "04C3D4E5");
    schedule_grant_exit_override("04C3D4E5");
    access_result_t r6_out_ovr = access_control_evaluate_sync(RFID_READER_EXIT, "04C3D4E5");
    bool t6 = (r6_in == ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY) &&
              (r6_out_deny == ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT) &&
              (r6_out_ovr == ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT) &&
              (occupancy_ledger_get_count() == 0);
    printf(" [Test 06] Schedule Entry/Exit Block & Supv. Override  : %s\n", t6 ? "PASS" : "FAIL");
    if (t6) passed++;

    /* Test 7: Emergency Evacuation Mode After-Hours Safe Egress */
    time_sync_set_datetime(2026, 10, 24, 10, 0, 0);
    access_control_evaluate_sync(RFID_READER_ENTRY, "04D4E5F6"); /* David enters */
    time_sync_set_datetime(2026, 10, 24, 23, 0, 0); /* After hours */
    schedule_set_emergency_mode(true);
    door_set_emergency_all(true);
    access_result_t r7_evac = access_control_evaluate_sync(RFID_READER_EXIT, "04D4E5F6");
    schedule_set_emergency_mode(false);
    door_set_emergency_all(false);
    bool t7 = (r7_evac == ACCESS_RESULT_GRANTED_EMERGENCY_EVAC) && (occupancy_ledger_get_count() == 0);
    printf(" [Test 07] Emergency Evacuation Mode Safe Egress       : %s\n", t7 ? "PASS" : "FAIL");
    if (t7) passed++;

    /* Test 8: Lecture Room PIR Occupancy -> Light ON -> Grace -> Vacancy Timeout OFF */
    pir_sensor_simulate_motion(1, true);
    room_light_status_t l_on;
    light_get_room_status(1, &l_on);
    pir_sensor_simulate_motion(1, false);
    for (int i = 0; i < 22; i++) {
        light_tick_1hz();
    }
    room_light_status_t l_off;
    light_get_room_status(1, &l_off);
    bool t8 = l_on.relay_on && (l_on.fsm_state == LIGHT_FSM_ON_ACTIVE) &&
              (!l_off.relay_on) && (l_off.fsm_state == LIGHT_FSM_OFF_VACANT) &&
              (l_off.energy_saved_mwh > 0);
    printf(" [Test 08] Room PIR -> Light ON -> Grace -> Timeout OFF: %s\n", t8 ? "PASS" : "FAIL");
    if (t8) passed++;

    /* Test 9: Node Offline Store-and-Forward & Idempotent Replay on Reconnect */
    net_protocol_set_node_online(NODE_ID_ROOM_2, false);
    pir_sensor_simulate_motion(2, true);
    pir_sensor_simulate_motion(2, false);
    node_net_status_t n2_off;
    net_protocol_get_node_status(NODE_ID_ROOM_2, &n2_off);
    net_protocol_set_node_online(NODE_ID_ROOM_2, true);
    node_net_status_t n2_on;
    net_protocol_get_node_status(NODE_ID_ROOM_2, &n2_on);
    bool t9 = (n2_off.buffered_offline_count >= 2) &&
              (n2_on.buffered_offline_count == 0) &&
              (n2_on.replayed_count >= 2) &&
              (n2_on.dup_suppressed_count >= 1);
    printf(" [Test 09] Offline Store-and-Forward & Dedup Replay    : %s\n", t9 ? "PASS" : "FAIL");
    if (t9) passed++;

    /* Test 10: Salted SHA-256 Admin Auth Lockout & Log Hash-Chain Integrity */
    auth_login("admin", "bad1", NULL);
    auth_login("admin", "bad2", NULL);
    auth_login("admin", "bad3", NULL);
    esp_err_t lock_err = auth_login("admin", CFG_AUTH_DEFAULT_PASS, NULL);
    auth_reset_lockout();
    esp_err_t ok_err = auth_login("admin", CFG_AUTH_DEFAULT_PASS, NULL);
    uint32_t checked = 0;
    bool chain_ok = false;
    logger_verify_integrity(&checked, &chain_ok);
    bool t10 = (lock_err == ESP_ERR_INVALID_STATE) && (ok_err == ESP_OK) && chain_ok && (checked > 0);
    printf(" [Test 10] Salted SHA-256 Auth Lockout & Hash Chain    : %s\n", t10 ? "PASS" : "FAIL");
    if (t10) passed++;

    /* Restore clock to Saturday 08:35:00 EAT */
    time_sync_set_datetime(2026, 10, 24, 8, 35, 0);

    printf("------------------------------------------------------------------------------\n");
    printf(" SELF-TEST SUMMARY: %d / %d SCENARIOS PASSED (%s)\n",
           passed, total, (passed == total) ? "ALL REQUIREMENTS VERIFIED" : "CHECK LOGS");
    printf("==============================================================================\n");
}

static void cmd_sim(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sim scan <entry|exit> <uid> | sim motion <1|2> <on|off> | sim node <1|2> <online|offline> | sim door_hold <entry|exit> | sim selftest\n");
        return;
    }

    if (strcasecmp(argv[1], "selftest") == 0) {
        run_selftest_suite();
        return;
    }

    if (strcasecmp(argv[1], "scan") == 0 && argc >= 4) {
        rfid_reader_t rdr = (strcasecmp(argv[2], "exit") == 0) ? RFID_READER_EXIT : RFID_READER_ENTRY;
        rfid_inject_scan(rdr, argv[3]);
        vTaskDelay(pdMS_TO_TICKS(80));
        return;
    }

    if (strcasecmp(argv[1], "motion") == 0 && argc >= 4) {
        uint8_t room = (uint8_t)atoi(argv[2]);
        bool on = (strcasecmp(argv[3], "on") == 0 || strcmp(argv[3], "1") == 0);
        if (pir_sensor_simulate_motion(room, on) == ESP_OK) {
            printf("OK: Simulated PIR motion %s in Lecture Room %u.\n", on ? "ACTIVE" : "IDLE", (unsigned)room);
        } else {
            printf("ERROR: Room must be 1 or 2.\n");
        }
        return;
    }

    if (strcasecmp(argv[1], "node") == 0 && argc >= 4) {
        int r = atoi(argv[2]);
        if (r != 1 && r != 2) {
            printf("ERROR: Node must be 1 (Room 1) or 2 (Room 2).\n");
            return;
        }
        bool online = (strcasecmp(argv[3], "online") == 0 || strcasecmp(argv[3], "on") == 0);
        net_protocol_set_node_online((node_id_t)r, online);
        printf("OK: Node %d (Lecture Room %d) link state set to %s.\n",
               r, r, online ? "ONLINE" : "OFFLINE (Store-and-Forward Active)");
        return;
    }

    if (strcasecmp(argv[1], "door_hold") == 0 && argc >= 3) {
        door_id_t d = (strcasecmp(argv[2], "exit") == 0) ? DOOR_ID_EXIT : DOOR_ID_ENTRY;
        door_simulate_forced_hold(d);
        printf("OK: Triggered forced/held-open tailgate alarm on %s door.\n",
               (d == DOOR_ID_ENTRY) ? "ENTRY" : "EXIT");
        return;
    }

    printf("Unknown sim command. Type 'help' for syntax.\n");
}

esp_err_t cli_execute_line(char *line)
{
    if (line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *argv[12] = { 0 };
    int argc = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(line, " \t\r\n", &saveptr);
    while (tok != NULL && argc < 12) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    if (argc == 0) {
        return ESP_OK;
    }

    const char *cmd = argv[0];
    if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help();
    } else if (strcasecmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcasecmp(cmd, "inside") == 0 || strcasecmp(cmd, "occupancy") == 0) {
        cmd_inside();
    } else if (strcasecmp(cmd, "rooms") == 0 || strcasecmp(cmd, "lights") == 0) {
        cmd_rooms();
    } else if (strcasecmp(cmd, "log") == 0) {
        cmd_log(argc, argv);
    } else if (strcasecmp(cmd, "stats") == 0) {
        cmd_stats();
    } else if (strcasecmp(cmd, "nodes") == 0) {
        cmd_nodes();
    } else if (strcasecmp(cmd, "daily_report") == 0) {
        cmd_daily_report();
    } else if (strcasecmp(cmd, "login") == 0) {
        if (argc < 3) {
            printf("Usage: login <username> <password> [admin_rfid_uid]\n");
        } else {
            const char *uid = (argc >= 4) ? argv[3] : NULL;
            esp_err_t err = auth_login(argv[1], argv[2], uid);
            if (err == ESP_OK) {
                printf("OK: Authenticated as '%s'. Admin commands unlocked.\n", argv[1]);
            } else if (err == ESP_ERR_INVALID_STATE) {
                auth_status_t st;
                auth_get_status(&st);
                printf("LOCKED OUT: Too many failed attempts. Wait %lu seconds.\n",
                       (unsigned long)st.lockout_remaining_sec);
            } else {
                printf("DENIED: Invalid admin credentials.\n");
            }
        }
    } else if (strcasecmp(cmd, "logout") == 0) {
        auth_logout();
        printf("OK: Admin session logged out.\n");
    } else if (strcasecmp(cmd, "passwd") == 0) {
        if (!require_admin()) {
            return ESP_OK;
        }
        if (argc < 3) {
            printf("Usage: passwd <old_password> <new_password>\n");
        } else if (auth_change_password(argv[1], argv[2]) == ESP_OK) {
            printf("OK: Admin password re-salted and updated in NVS.\n");
        } else {
            printf("ERROR: Password change failed (check old password and min length 6).\n");
        }
    } else if (strcasecmp(cmd, "schedule") == 0) {
        cmd_schedule(argc, argv);
    } else if (strcasecmp(cmd, "override_exit") == 0) {
        if (!require_admin()) {
            return ESP_OK;
        }
        if (argc < 2) {
            printf("Usage: override_exit <uid>\n");
        } else {
            schedule_grant_exit_override(argv[1]);
            logger_log_audit("admin", "SUPERVISOR_EXIT_PASS", argv[1]);
            printf("OK: Granted 5-minute supervisor safe-exit override for UID %s.\n", argv[1]);
        }
    } else if (strcasecmp(cmd, "emergency") == 0) {
        if (!require_admin()) {
            return ESP_OK;
        }
        if (argc < 2) {
            printf("Usage: emergency <on|off>\n");
        } else {
            bool on = (strcasecmp(argv[1], "on") == 0);
            schedule_set_emergency_mode(on);
            door_set_emergency_all(on);
            net_protocol_broadcast_emergency(on);
            logger_log_audit("admin", on ? "EMERGENCY_ON" : "EMERGENCY_OFF", "CLI command");
            printf("!! EMERGENCY EVACUATION MODE %s !!\n", on ? "ACTIVATED (ALL DOORS UNLOCKED)" : "CLEARED");
            if (on) {
                printf("Roll-Call of Occupants Currently Inside Building:\n");
                cmd_inside();
            }
        }
    } else if (strcasecmp(cmd, "user") == 0) {
        cmd_user(argc, argv);
    } else if (strcasecmp(cmd, "sd") == 0) {
        cmd_sd(argc, argv);
    } else if (strcasecmp(cmd, "time") == 0) {
        cmd_time(argc, argv);
    } else if (strcasecmp(cmd, "sim") == 0) {
        cmd_sim(argc, argv);
    } else {
        printf("Unknown command '%s'. Type 'help' for available commands.\n", cmd);
    }

    return ESP_OK;
}

static void cli_task(void *arg)
{
    (void)arg;
    char line_buf[160];
    size_t pos = 0;

    printf("\n==============================================================================\n");
    printf("  %s (%s)\n", CFG_SYSTEM_NAME, CFG_FIRMWARE_VERSION);
    printf("  College of Computing and Information Sciences (CoCIS), Makerere University\n");
    printf("  Type 'help' for commands, 'status' for overview, or 'sim selftest' to test.\n");
    printf("==============================================================================\n");
    print_prompt();

    while (1) {
        uint8_t ch = 0;
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            printf("\n");
            line_buf[pos] = '\0';
            if (pos > 0) {
                cli_execute_line(line_buf);
                pos = 0;
            }
            print_prompt();
        } else if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (isprint(ch) && pos + 1 < sizeof(line_buf)) {
            line_buf[pos++] = (char)ch;
            putchar((int)ch);
            fflush(stdout);
        }
    }
}

esp_err_t cli_init(void)
{
    uart_config_t ucfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &ucfg);

    if (xTaskCreate(cli_task, "cli_task", 8192, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Interactive UART0 management CLI initialized");
    return ESP_OK;
}
