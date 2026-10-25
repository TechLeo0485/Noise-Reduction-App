# VoiceStudio User Guide

VoiceStudio cleans up your microphone before Zoom, Teams, Discord, Google Meet or any other app hears it. It removes background noise (fans, keyboards, traffic) and can even remove **other people's voices**, so only you are heard. Everything runs on your own computer: no account, no internet, no subscription.

---

## 1. Install

1. Run **VoiceStudio-setup.exe**.
   If Windows shows *"Windows protected your PC"*, click **More info → Run anyway**.
2. Keep **Install VB-CABLE** ticked. VB-CABLE is the free "virtual microphone" that carries your cleaned voice to other apps.
   Windows asks once *"Would you like to install this device software?"*. Click **Install**.
3. Optional: **Start VoiceStudio with Windows**. Recommended if you use it every day.
4. **Restart your computer** when setup asks. VB-CABLE needs the restart.

> VB-CABLE is donationware by VB-Audio Software (**vb-cable.com**). If you find it useful, you can support its author at vb-audio.com.

---

## 2. First-time setup (2 minutes)

Open VoiceStudio. The **Home** page shows a checklist. Make every item green:

| Checklist item | What to do |
|---|---|
| **Microphone** | If it shows the wrong device, click **Use …** or go to **Devices**. |
| **Virtual cable installed** | Done by the setup. If not, see Troubleshooting. |
| **Sending your voice to the virtual cable** | Click **Fix it** if it is not green. |
| **We can hear you** | Say something; the meter should move. |

### Choosing your microphone (Devices page)

* **1. Your microphone**: click the microphone you speak into. A green bar under it shows **"Hearing you"** when you talk.
  *Windows default microphone* follows whatever Windows uses.
* **2. Send your voice to apps**: should say **✓ Sending your voice to "CABLE Input"**. If not, click **Use CABLE Input**.
* **3. Headphones** (optional): turn on **Listen live** to hear yourself. Use headphones, not speakers, or you get an echo.

---

## 3. Use it in your call app

In your call app, pick **CABLE Output** as the **microphone**:

| App | Where |
|---|---|
| **Zoom** | Settings → Audio → Microphone → **CABLE Output (VB-Audio Virtual Cable)** |
| **Microsoft Teams** | Settings → Devices → Microphone → **CABLE Output** |
| **Discord** | User Settings → Voice & Video → Input Device → **CABLE Output** |
| **Google Meet** | ⋮ → Settings → Audio → Microphone → **CABLE Output** |
| **Windows (all apps)** | Settings → System → Sound → Input → **CABLE Output** |

**Tip:** VoiceStudio already removes noise. Set the call app's own noise suppression to **Low** or **Off**. Two noise removers in a row can make your voice sound thin.

Your speakers and headphones in the call app stay unchanged.

---

## 4. Remove other people's voices (Strict voice isolation)

This is for situations like a job interview where others may talk in the same room.

1. Go to **Sound** (or **Noise Reduction**). Find **Strict voice isolation**.
2. Click **Record my voice (10 s)**. Read the text on screen in your normal voice.
   * Be **alone and in a quiet room** while recording.
   * Use the **same microphone** you will use in the call.
3. Turn on **Only send my voice**.

### What to expect

| Situation | Result |
|---|---|
| Someone talks while you are silent | Removed, usually completely |
| Someone keeps talking after you stop | Removed within about a second |
| Someone talks **at the same moment** as you, quieter than you | May still be faintly audible underneath your voice |
| Your own voice | Sounds natural, slightly less "bright" |

**For the best results:** use a headset microphone or keep the microphone close to your mouth (5–15 cm). Keep other people at least 2 metres away from the microphone.

### Good to know

* Your voice reaches the other side about **0.08 seconds later** while isolation is on. That's not noticeable in conversation.
* **Safety first:** while isolation is on, VoiceStudio never sends your raw microphone. If isolation cannot run (for example right after starting, or without a recorded voice), your microphone is **silenced** and a yellow warning explains why.
* If you switch to a **different microphone**, VoiceStudio reminds you to record your voice again.
* **Re-record** any time. **Delete my voice** removes the recording from your computer.

---

## 5. Sound settings

* **Noise removal** (Sound page): *Off*, *Light*, *Balanced* or *Strong*. *Balanced* suits most rooms.
* **Voice style**: ready-made settings such as **Interview**, **Natural Voice**, **Gaming** and **Streaming**.
* **Microphone volume** (Home): aim for the meter to reach the middle when you speak normally.
* **Mute**: the big button on Home, or right-click the tray icon → **Mute microphone**.

### Check how you sound

On **Home**, click **Record 5 s and play back**. VoiceStudio plays back exactly what other people hear.

---

## 6. Running in the background

* **Closing the window does not stop VoiceStudio.** It keeps cleaning your voice and lives in the notification area next to the clock. You may need to click **^** to see the icon. Drag it onto the taskbar to keep it visible.
* **Click** the icon to open the window. **Right-click** it to mute, switch strict isolation on or off, or **Quit**.
* **Settings → Start with Windows** starts VoiceStudio automatically (hidden in the tray) when you sign in.
* **Settings → Keep running when I close the window**: turn it off if you prefer that closing the window quits.

---

## 7. Troubleshooting

**People in the call can't hear me**

1. In the call app the microphone must be **CABLE Output** (not your real microphone, not "CABLE Input").
2. In VoiceStudio → **Devices**: the green line must say **Sending your voice to "CABLE Input"**.
3. VoiceStudio must be running (tray icon visible) and not muted.
4. Click **Test virtual microphone** on the Devices page.

**There is a yellow "microphone is silenced" warning.**
Strict voice isolation is on but cannot run yet. Record your voice, or wait a few seconds after starting. If it says isolation is unavailable, switch strict isolation off.

**The other person's voice still comes through.**
See *What to expect* in section 4. Move the microphone closer to you, move others away, and record your voice again in a quiet room with the microphone you use for calls.

**My voice sounds choppy or has small gaps.**
Close heavy programs (games, video editing). VoiceStudio adjusts itself if the computer is too busy. Under **Settings → Speed vs. stability**, choose a more stable option.

**"CABLE Output" / "CABLE Input" does not appear.**
Restart Windows after installing. Still missing? Run the setup again with **Install VB-CABLE** ticked, or install it by hand from vb-audio.com/Cable.

**I hear an echo of myself.**
Turn off **Listen live**, or use headphones instead of speakers.

**After unplugging a headset, the microphone stopped.**
VoiceStudio reconnects automatically. If it does not, open **Devices** and click your microphone again.

**Still stuck?**
In **Settings → Help and troubleshooting**, click **Copy diagnostics** and paste it to whoever helps you. It contains settings and device names only, never audio.

---

## 8. Privacy

* All audio is processed **on your computer** and is never sent anywhere. VoiceStudio does not use the internet.
* The only audio saved to disk is your **voice sample** for strict isolation (`%LOCALAPPDATA%\VoiceStudio\voice`). **Delete my voice** removes it.
* Logs contain events and device names only.

---

## 9. Uninstall

Windows **Settings → Apps → VoiceStudio → Uninstall**. You will be asked whether to delete your settings and voice sample as well.
VB-CABLE stays installed (other apps may use it). Remove it separately under *Apps* if you no longer need it.

---

## Credits

* **VB-CABLE** virtual audio cable by VB-Audio Software. Donationware, **vb-cable.com**.
* **VoxLock** voice isolation model by Arbaz Siddiqui (Apache-2.0).
* **RNNoise** noise suppression by Xiph.Org (BSD).
* **ONNX Runtime** by Microsoft (MIT) and **Qt** (LGPL-3.0).
