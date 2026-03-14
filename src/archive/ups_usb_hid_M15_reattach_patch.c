
/*
Milestone 15 – USB Reattach Recovery Patch
Fixes UPS reconnect after unplug.
*/

static void cleanup_device(void)
{
    if (s_dev) {
        usb_host_device_close(s_client, s_dev);
        s_dev = NULL;
    }

    s_dev_connected = false;
    s_dev_gone = false;
    s_new_dev_addr = 0;

    reset_session();

    ESP_LOGI(TAG, "USB device cleanup complete. Waiting for NEW_DEV...");
}

/* replace DEV_GONE handling block with this */

if (s_dev_gone) {
    ESP_LOGW(TAG, "DEV_GONE received");

    cleanup_device();
}
