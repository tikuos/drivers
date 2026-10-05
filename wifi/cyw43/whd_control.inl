/*
 * Tiku Drivers
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * whd_control.inl - CYW43439 scan, join, leave and forget requests.
 *
 * Each request is validated, queued for the runner and published as state
 * without radio I/O.  Included by whd.c after the runner state; TikuBench
 * compiles it on the host with queue and NVM doubles.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Report whether the runner is scanning, joining or leaving.
 *
 * The scan and join loops wait on their own timers and do not see a new
 * request, and a queued disconnect has not run yet, so requests are refused.
 *
 * @return Non-zero while a scan, a join or a queued disconnect is pending
 */
static int wifi_runner_busy(void)
{
    return cyw43_state.scan_in_progress != 0U ||
           cyw43_state.disconnect_pending != 0U ||
           cyw43_state.link_state == TIKU_WIRELESS_LINK_CONNECTING;
}

/**
 * @brief Trigger an active scan (non-blocking).
 *
 * @return TIKU_DRV_OK on enqueue; TIKU_DRV_ERR_INVALID if the radio is not
 *         up; TIKU_DRV_ERR_TIMEOUT while busy or when the queue is full
 */
int tiku_wireless_scan_start(void)
{
    if (!cyw43_state.up) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (wifi_runner_busy() ||
        !tiku_process_post(&cyw43_runner, TIKU_WIRELESS_EVT_SCAN_START,
                           NULL)) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    cyw43_state.scan_in_progress = 1U;
    return TIKU_DRV_OK;
}

/**
 * @brief Queue a join to a WPA2-PSK or WPA3-SAE network.
 *
 * While joined, the runner's join leaves the current network for this one.
 *
 * @param ssid  Network SSID (1..32 chars, null-terminated)
 * @param psk   Passphrase (WPA2: 8..63 chars; WPA3: 1..63 chars)
 * @param auth  TIKU_WIRELESS_AUTH_WPA2_PSK or _WPA3_SAE
 * @return TIKU_DRV_OK on enqueue; TIKU_DRV_ERR_INVALID for a bad profile or
 *         a radio that is not up; TIKU_DRV_ERR_TIMEOUT while busy or when
 *         the queue is full
 */
int tiku_wireless_connect_auth(const char *ssid, const char *psk,
                               tiku_wireless_auth_t auth)
{
    uint8_t s;
    uint8_t p;
    uint8_t i;

    if ((auth != TIKU_WIRELESS_AUTH_WPA2_PSK &&
         auth != TIKU_WIRELESS_AUTH_WPA3_SAE) ||
        !wifi_profile_lengths(ssid, psk, (uint8_t)auth, &s, &p) ||
        !cyw43_state.up) {
        return TIKU_DRV_ERR_INVALID;
    }
    /* post() only queues, so the target is written after it succeeds: a
     * refused request leaves the previous target, link state and reconnect
     * policy as they were. */
    if (wifi_runner_busy() ||
        !tiku_process_post(&cyw43_runner, TIKU_WIRELESS_EVT_JOIN_START,
                           NULL)) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    for (i = 0U; i < sizeof cyw43_state.target_ssid; i++) {
        cyw43_state.target_ssid[i] = (i < s) ? ssid[i] : '\0';
    }
    for (i = 0U; i < sizeof cyw43_state.target_psk; i++) {
        cyw43_state.target_psk[i] = (i < p) ? psk[i] : '\0';
    }
    cyw43_state.target_ssid_len = s;
    cyw43_state.target_auth = (uint8_t)auth;
    cyw43_state.link_state = TIKU_WIRELESS_LINK_CONNECTING;
    cyw43_state.user_disconnected = 0U;
    cyw43_state.reconnect_attempts = 0U;
    return TIKU_DRV_OK;
}

/**
 * @brief Queue a WPA2-PSK join; see tiku_wireless_connect_auth().
 *
 * @param ssid  Network SSID
 * @param psk   WPA2 passphrase
 * @return As tiku_wireless_connect_auth()
 */
int tiku_wireless_connect(const char *ssid, const char *psk)
{
    return tiku_wireless_connect_auth(ssid, psk, TIKU_WIRELESS_AUTH_WPA2_PSK);
}

/**
 * @brief Queue a disconnect and stop auto-reconnect.
 *
 * @return TIKU_DRV_OK on enqueue; TIKU_DRV_ERR_INVALID if the radio is not
 *         up; TIKU_DRV_ERR_TIMEOUT while busy or when the queue is full
 */
int tiku_wireless_disconnect(void)
{
    if (!cyw43_state.up) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (wifi_runner_busy() ||
        !tiku_process_post(&cyw43_runner, TIKU_WIRELESS_EVT_DISCONNECT,
                           NULL)) {
        return TIKU_DRV_ERR_TIMEOUT;
    }
    cyw43_state.user_disconnected = 1U;
    cyw43_state.disconnect_pending = 1U;
    return TIKU_DRV_OK;
}

/**
 * @brief Leave the network and erase the saved profile.
 *
 * The keys are not cleared while the runner may still be using them for a
 * join: a busy radio refuses, and the record is left as it was.
 *
 * @return TIKU_DRV_OK; TIKU_DRV_ERR_TIMEOUT while busy or when the queue is
 *         full; TIKU_DRV_ERR_IO when the NVM flush fails
 */
int tiku_wireless_forget(void)
{
    int rc;

    if (cyw43_state.up) {
        rc = tiku_wireless_disconnect();
        if (rc != TIKU_DRV_OK) {
            return rc;
        }
    }
    cyw43_state.user_disconnected = 1U;
    cyw43_state.boot_rejoin_done = 1U;
    cyw43_state.target_ssid_len = 0U;
    cyw43_state.reconnect_attempts = 0U;
    wifi_clear_bytes(cyw43_state.target_psk, sizeof cyw43_state.target_psk);
    wifi_clear_bytes(cyw43_state.target_ssid, sizeof cyw43_state.target_ssid);
    return wifi_cred_forget();
}

/**
 * @brief Record the outcome of the runner's disassociation request.
 *
 * The link is marked failed only when it was joined and the firmware refused
 * to leave; a refused disassociation of a radio that was not joined is idle.
 *
 * @param rc          Result of the DISASSOC ioctl
 * @param was_joined  Non-zero if the link was joined before the request
 */
static void wifi_disconnect_done(int rc, uint8_t was_joined)
{
    cyw43_state.disconnect_pending = 0U;
    if (rc != TIKU_DRV_OK && was_joined) {
        cyw43_state.link_state = TIKU_WIRELESS_LINK_FAILED;
        cyw43_state.link_status_raw = (uint32_t)rc;
    } else {
        cyw43_state.link_state = TIKU_WIRELESS_LINK_IDLE;
    }
    cyw43_state.joined_ssid_len = 0U;
    cyw43_state.target_ssid_len = 0U;   /* prevents auto-reconnect */
    cyw43_state.user_disconnected = 1U;
    cyw43_state.reconnect_attempts = 0U;
    cyw43_state.rssi_dbm = 0;
    (void)tiku_process_post(TIKU_PROCESS_BROADCAST,
                            TIKU_WIRELESS_EVT_LINK_DOWN,
                            (tiku_event_data_t)(uintptr_t)0);
}
