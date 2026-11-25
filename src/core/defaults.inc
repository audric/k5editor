// Settings a new image starts with (blankImage): {setting id, value}. A list value is the
// position in the setting's options. Settings not named here start as "not set".
// Radio
{"squelch", 1},                             // Squelch (Sql)
{"rx_mode", 0},                             // RX mode: MAIN ONLY
{"TX_VFO", 1},                              // Main VFO: B
{"tot", 11},                                // Max TX time (TxTOut): 1 min
{"set_pwr", 6},                             // Power used by channels set to USER (SetPwr): 5W
{"mic_gain", 3},                            // Mic gain (Mic): +12.0dB
{"vox", 0},                                 // Voice-operated transmit (VOX): OFF
{"roger_beep", 0},                          // End of transmission beep (Roger): OFF
{"ste", false},                             // Squelch tail elimination (STE)
{"rp_ste", 0},                              // Repeater squelch tail elimination (RP STE): OFF
{"set_nfm", 0},                             // Narrow FM width (SetNFM): NARROW
{"set_ptt", 0},                             // PTT mode (SetPtt): CLASSIC
{"set_tot", 0},                             // TX timeout indicator (SetTot): OFF
{"set_eot", 0},                             // End of transmission indicator (SetEot): OFF
{"call_channel", 0},                        // One-key call channel (1 Call): Channel M1
{"freq_mode_allowed", true},                // Frequency (VFO) mode allowed
{"battery_save", 4},                        // Battery saver (BatSav): 1:4
{"Battery_type", 0},                        // Battery type (BatTyp): 1600 mAh K5
{"alarm_mode", 0},                          // Alarm mode: SITE
// Display and sound
{"channel_display_mode", 3},                // Channel display (ChDisp): Name + frequency
{"welcome_mode", 1},                        // Power-on screen (POnMsg): Two short sounds (SOUND)
{"logo1", "QUANSHENG"},                     // Power-on message line 1
{"logo2", "UV-K5"},                         // Power-on message line 2
{"battery_text", 0},                        // Battery level display (BatTXT): NONE
{"backlight_time", 5},                      // Backlight time (BLTime): 25 sec
{"backlight_min", 0},                       // Minimum backlight level (BLMin): 0
{"backlight_max", 10},                      // Maximum backlight level (BLMax): 10
{"backlight_on_TX_RX", 0},                  // Backlight on TX/RX (BLTxRx): OFF
{"mic_bar", true},                          // Microphone level bar (MicBar)
{"button_beep", false},                     // Keypad beep (Beep)
{"s0_level", -130},                         // S-meter S0 level (dBm)
{"s9_level", -76},                          // S-meter S9 level (dBm)
// Keys and lock
{"key1_shortpress_action", 2},              // Side key 1, short press (F1Shrt): POWER
{"key1_longpress_action", 3},               // Side key 1, long press (F1Long): MONITOR
{"key2_shortpress_action", 1},              // Side key 2, short press (F2Shrt): FLASHLIGHT
{"key2_longpress_action", 6},               // Side key 2, long press (F2Long): ALARM
{"keyM_longpress_action", 0},               // Menu key, long press (M Long): NONE
{"key_lock", false},                        // Keypad locked
{"auto_keypad_lock", 0},                    // Lock keypad automatically after (KeyLck): OFF
{"set_key", 0},                             // Key used by RescueOps (SetKEY): MENU
{"set_menu_lock", 0},                       // RescueOps menu lock: OFF
{"set_tmr", 1},                             // Timer (SetTmr): ON
{"set_off_tmr", 60},                        // Switch off after (SetOff): 1h:00m
// Scanning
{"scan_resume_mode", 2},                    // Scan resume (ScnRev): CARRIER 0.50 s: resume this long after the signal disappears
{"slDef", 0},                               // Default scan list (SList): No list
{"sl1PriorEnab", false},                    // List 1: priority channel scan
{"sl1PriorCh1", 0},                         // List 1: priority channel 1: None
{"sl1PriorCh2", 0},                         // List 1: priority channel 2: None
{"sl2PriorEnab", false},                    // List 2: priority channel scan
{"sl2PriorCh1", 0},                         // List 2: priority channel 1: None
{"sl2PriorCh2", 0},                         // List 2: priority channel 2: None
{"sl3PriorEnab", false},                    // List 3: priority channel scan
{"sl3PriorCh1", 0},                         // List 3: priority channel 1: None
{"sl3PriorCh2", 0},                         // List 3: priority channel 2: None
// DTMF
{"dtmf_dtmf_local_code", "102"},            // Own ID (3 characters, 0-9 A-D)
{"dtmf_dtmf_up_code", "123"},               // Up code, sent at start of TX (UPCode)
{"dtmf_dtmf_down_code", "456"},             // Down code, sent at end of TX (DWCode)
{"dtmf_side_tone", true},                   // Play DTMF on the speaker when sending (D ST)
{"live_DTMF_decoder", true},                // Show received DTMF codes
{"dtmf_decode_response", 1},                // Decode response (D Resp): Ring
{"dtmf_preload_time", 300},                 // Pre-load time, ms (D Prel)
{"dtmf_first_code_persist_time", 100},      // First code persist time, ms
{"dtmf_hash_persist_time", 100},            // * and # persist time, ms
{"dtmf_code_persist_time", 100},            // Code persist time, ms
{"dtmf_code_interval_time", 100},           // Code interval time, ms
{"dtmf_separate_code", ""},                 // Separate code (one of A-D * #)
{"dtmf_group_call_code", ""},               // Group call code (one of A-D * #)
{"dtmf_permit_remote_kill", false},         // Permit remote kill
{"dtmf_kill_code", "77777"},                // Kill code (5 characters, 0-9 A-D)
{"dtmf_revive_code", "88888"},              // Revive code (5 characters, 0-9 A-D)
// Unlock
{"int_flock", 0},                           // TX frequency lock (F Lock): DEFAULT+ (137-174, 400-470)
{"int_350en", false},                       // Receive 350-400 MHz (350 En)
{"int_scren", false},                       // Scrambler enabled (ScraEn)
