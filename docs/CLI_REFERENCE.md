# Serial Command-Line Interface Reference (`CLI_REFERENCE.md`)

**Project:** CoCIS Smart Control and Monitoring System (Makerere University)  
**Interface:** UART0 (`115200 8N1`) via Wokwi Serial Monitor (`$serialMonitor`)  
**Prompt:** `cocis> ` (or `cocis(admin)# ` when authenticated as Administrator)

---

## 1. Command Summary Table

| Category | Command Syntax | Auth Required? | Description |
| :--- | :--- | :---: | :--- |
| **General** | `help` | No | Print categorized command reference table |
| **Monitoring** | `status` | No | Show system overview (Time, Mode, Schedule, Headcount, SD, Wi-Fi, Auth) |
| **Monitoring** | `inside` / `occupancy` | No | List every person currently inside CoCIS with role, entry time, and duration |
| **Monitoring** | `rooms` / `lights` | No | Show Lecture Room 1 & Room 2 occupancy, light FSM state, timeouts, and energy saved |
| **Monitoring** | `log [n]` | No | Display the most recent `n` (default `15`) hash-chained access/system log records |
| **Monitoring** | `log user <uid>` | No | Filter and display all access history records for a specific RFID tag `<uid>` |
| **Monitoring** | `stats` | No | Show access grant/deny counters by reason, peak occupancy, and energy savings |
| **Monitoring** | `nodes` | No | Show distributed terminal health (`NODE_ENTRANCE`, `NODE_ROOM_1`, `NODE_ROOM_2`), seq IDs, heartbeats, and store-and-forward buffer counts |
| **Monitoring** | `daily_report` | No | Generate a comprehensive daily executive report for CoCIS management |
| **Admin Auth** | `login <user> <pass> [uid]` | No | Authenticate an admin session (salted SHA-256 + optional 2FA Admin RFID UID) |
| **Admin Auth** | `logout` | Yes | Terminate current administrator session immediately |
| **Admin Auth** | `passwd <old> <new>` | Yes | Change administrator password (re-salts and stores new SHA-256 digest in NVS) |
| **Schedule** | `schedule show` | No | Display weekly authorized building access windows (`Mon`–`Sun`) and override states |
| **Schedule** | `schedule set <day\|all> <HH:MM> <HH:MM>` | **Yes (Admin)** | Configure authorized open/close time window (e.g., `schedule set mon 07:00 21:00`) |
| **Schedule** | `schedule close <day>` | **Yes (Admin)** | Mark an entire day of the week as closed (`00:00–00:00`, disabled) |
| **Safety/Egress**| `override_exit <uid>` | **Yes (Admin)** | Grant a 5-minute supervisor safe-exit pass for `<uid>` to exit after closing hours |
| **Safety/Egress**| `emergency <on\|off>` | **Yes (Admin)** | Activate/clear building-wide Emergency Evacuation Mode and print roll-call list |
| **User Mgmt** | `user list` | No | List all enrolled RFID tags, names, roles, active flags, andinside status |
| **User Mgmt** | `user add <uid> <role> <name>` | **Yes (Admin)** | Enroll a new RFID tag (`admin`, `staff`, `student`, `visitor`) |
| **User Mgmt** | `user del <uid>` | **Yes (Admin)** | Remove an enrolled RFID user from `user_db` |
| **User Mgmt** | `user enable <uid>` | **Yes (Admin)** | Activate a disabled user tag |
| **User Mgmt** | `user disable <uid>` | **Yes (Admin)** | Deactivate/revoke a user tag without deleting its history |
| **Storage/Time** | `sd status` | No | Show SPI microSD mount status, CSV file sizes, and hash-chain tip |
| **Storage/Time** | `sd verify` | No | Recompute SHA-256 hash chain across SD logs to detect any tampering |
| **Storage/Time** | `sd format` | **Yes (Admin)** | Format/reset SD CSV log files and re-initialize genesis hash chain |
| **Storage/Time** | `time [set YYYY-MM-DD HH:MM:SS]` | **Set: Admin** | Show current East Africa Time (`EAT`) or manually set the system wall clock |
| **Simulation** | `sim scan <entry\|exit> <uid>` | No | Inject an RFID scan at the Entry or Exit reader |
| **Simulation** | `sim motion <1\|2> <on\|off>` | No | Simulate PIR motion state in Lecture Room 1 or Lecture Room 2 |
| **Simulation** | `sim node <1\|2> <online\|offline>` | No | Disconnect/reconnect a lecture room terminal to test store-and-forward replay |
| **Simulation** | `sim door_hold <entry\|exit>` | No | Simulate a door propped/forced open beyond threshold to trigger tailgate alarm |
| **Simulation** | `sim selftest` | No | Run the automated 10-scenario end-to-end verification suite |

---

## 2. Pre-Enrolled Default CoCIS RFID Database & Admin Credentials

### Default Admin Serial CLI Login
- **Username:** `admin`
- **Password:** `Cocis@2026`
- **Optional 2FA Admin RFID Tag:** `04A00001`

### Pre-Enrolled RFID Tags (`user list`)

| RFID Tag UID | Name | Role | Active? | Notes |
| :--- | :--- | :--- | :---: | :--- |
| `04A00001` | `Prof. Tonny Oyana` | `ADMIN` | `YES` | CoCIS Principal / Security Admin (can exit after hours) |
| `04A1B2C3` | `Matthew Mumbere` | `STUDENT` | `YES` | Bound to **Green Entry Button** & **Blue Exit Button** on Wokwi canvas |
| `04B2C3D4` | `Dr. Engineer Bainomugisha` | `STAFF` | `YES` | Senior Lecturer |
| `04C3D4E5` | `Sarah Namukasa` | `STUDENT` | `YES` | BSE III Student |
| `04D4E5F6` | `David Okello` | `STUDENT` | `YES` | CS II Student |
| `04E5F607` | `Grace Atim` | `STAFF` | `YES` | Lab Technologist |
| `04V10008` | `External Examiner` | `VISITOR` | `YES` | Temporary Visitor Badge (24-hour validity window) |
| `0499DEAD` | `Suspended Account` | `STUDENT` | `NO` | Inactive / revoked tag (triggers `DENY_USER_INACTIVE`) |
| `DEADBEEF` | *(Unenrolled)* | *(None)* | — | Bound to **Red Intruder Button** on Wokwi canvas (`DENY_UNKNOWN_TAG`) |

---

## 3. Example Command Outputs

### `status`
```text
========================================================================
          CoCIS SMART CONTROL & MONITORING SYSTEM - STATUS
========================================================================
 System Clock (EAT) : 2026-10-24 08:35:12 (SAT) [VALID - RTC+SNTP]
 Building Schedule  : OPEN (Today's Window: 08:00 - 17:00)
 Emergency Mode     : NORMAL OPERATION
 Live Occupancy     : 2 person(s) currently inside (Peak today: 3)
 Entry Door         : CLOSED (Locked)   | Exit Door : CLOSED (Locked)
 Lecture Room 1     : OCCUPIED          | Light 1   : ON (Active)
 Lecture Room 2     : VACANT            | Light 2   : OFF (Saved 142 mWh)
 SD Card Storage    : MOUNTED (/sdcard) | Hash Chain: 8F3A19C4 (Verified)
 Network / Nodes    : 3/3 Nodes ONLINE  | Wi-Fi     : CONNECTED (Wokwi-GUEST)
 Admin CLI Session  : AUTHENTICATED (admin, auto-logout in 284s)
========================================================================
```

### `inside`
```text
+----------+----------+------------------------+---------+---------------------+----------+
| Slot     | Tag UID  | Full Name              | Role    | Entry Time (EAT)    | Duration |
+----------+----------+------------------------+---------+---------------------+----------+
| 01       | 04A1B2C3 | Matthew Mumbere        | STUDENT | 2026-10-24 08:31:04 | 00:04:08 |
| 02       | 04B2C3D4 | Dr. Eng. Bainomugisha  | STAFF   | 2026-10-24 08:32:50 | 00:02:22 |
+----------+----------+------------------------+---------+---------------------+----------+
Total Currently Inside CoCIS: 2 person(s)
```

### `rooms`
```text
+--------+----------+---------------+------------+-------------+--------------+--------------+
| Room   | Node St. | PIR Motion    | Light FSM  | Vacancy Tmr | Light On (s) | Energy Saved |
+--------+----------+---------------+------------+-------------+--------------+--------------+
| Room 1 | ONLINE   | MOTION ACTIVE | ON_ACTIVE  | 15s / 15s   | 124 s        | 210 mWh      |
| Room 2 | ONLINE   | IDLE          | OFF_VACANT | 0s / 15s    | 45 s         | 485 mWh      |
+--------+----------+---------------+------------+-------------+--------------+--------------+
Estimated Building Electricity Saved: 695 mWh (UGX 2,780 equivalent scaled pilot)
```
