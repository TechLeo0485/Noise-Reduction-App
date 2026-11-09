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
| **Microphone** | If it shows the wrong device, click **Use …** or go to **Setup**. |
| **Virtual microphone found** | Done by the setup (VB-CABLE). If not, see Troubleshooting. |
| **Sending your voice to the virtual microphone** | Click **Fix it** if it is not green. |
| **We can hear you** | Say something; the meter should move. |

The checklist disappears once everything is green.

### The pages

| Page | What it is for |
|---|---|
| **Home** | Is everything working, live levels and delay, and the everyday choices: how you **sound like**, **background noise**, **only my voice**, **steady voice at any distance** and hearing yourself |
| **Voice** | The voice changer: pick a voice, adjust **Pitch**, **Thickness** and **Presence**, save your own voices, hear yourself; *More controls* holds tone, evening out, limiter and effect order |
| **Clean-up** | Background noise level and engine, ready-made settings per situation |
| **Only my voice** | Strict voice isolation, the **isolation model** and recording your voice |
| **Setup** | Microphone, virtual microphone, headphones, general settings, **Delay**, help; *Advanced* holds processor, speed, buffers, measurements and logging |

The sidebar holds the **On/Off** switch for VoiceStudio, **Mute microphone** and **Help and guide**. The top right
shows whether VoiceStudio is **Live** and the current **Delay** (click it to open Setup).
Expert settings sit in the **Advanced** card at the bottom of the page they belong to. Click a section to open it.

### Choosing your microphone (Setup page)

* **1. Your microphone**: click the microphone you speak into. A green bar under it shows **"Hearing you"** when you talk.
  *Windows default microphone* follows whatever Windows uses. If the Windows default is a virtual microphone
  (for example after you made *VoiceStudio Virtual Microphone* the default for your call apps), VoiceStudio uses
  your real microphone instead, so it never listens to its own output.
* **2. Send your voice to apps**: should say **✓ Sending your voice to "CABLE Input"** (or your own virtual microphone). If not, click **Use virtual microphone**.
* **3. Headphones** (optional): where you hear yourself. Turn on **Listen live** on Home. Use headphones, not speakers, or you get an echo.

### VoiceStudio Virtual Microphone (instead of VB-CABLE)

Setup can also install VoiceStudio's own free virtual microphone (task *Install the VoiceStudio Virtual Microphone*).
It is a test-signed driver, so it only works in **Windows Test Mode**, which needs **Secure Boot turned off** in the
PC's firmware settings. Setup explains this and asks before turning Test Mode on. Once installed, VoiceStudio sends
your voice to **VoiceStudio Mic Feed** by itself; in Discord, OBS, games or Windows pick
**VoiceStudio Virtual Microphone**. Details and risks: `driver/README.md`.

### Using your own virtual microphone

VoiceStudio works with any virtual audio driver, not only VB-CABLE. Devices with "cable" or "virtual" in their name are found automatically.
If yours is not, open **Setup → 2. Send your voice to apps → Use my own virtual microphone** and switch on both of its sides:
the **playback side** (VoiceStudio sends your voice there) and the **recording side** (call apps use it as the microphone).

---

## 3. Use it in your call app

In your call app, pick **CABLE Output** as the **microphone** (with your own virtual microphone: its recording side). Home shows the exact name to pick once everything is set up:

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

1. Go to **Only my voice** and click **Start recording** (card *My voice*).
2. Follow the guide on screen. It changes every 20 seconds: **normal**, **louder**, **quiet**, **higher**, **lower**, **with feeling**, then **free talk**.
   Talk as long as you like (1 to 3 minutes works great, up to 30 minutes), then click **Done**.
   * Be **alone and in a quiet room** while recording.
   * Use the **same microphone** you will use in the call.
3. Turn on **Strict voice isolation** (card *Only send my voice*, on Only my voice or Home).

The network that recognises you listens to 3 seconds of your voice. After you click **Done**, VoiceStudio tries several 3-second
samples from different parts of your recording (normal, loud, quiet, high, low...) on the rest of your recording and keeps the
one that keeps the most of your voice in **every** style. This takes up to about half a minute; your current voice keeps working meanwhile.

**Add more** records more and adds it to what you already recorded. The guide continues with the styles you have not recorded yet,
and the *My voice* card shows which styles are covered. **Start over** replaces the recording.
The whole recording is saved as `my_voice_recording.wav`. Click **Open folder** on the *My voice* card to find it.

### Choosing the isolation model

On **Only my voice**, card *Isolation model*. All models use the same recording of your voice, so you can switch any time
without recording again. Your microphone is silenced for a moment while the model switches.

| Model | Technology | Best for | Delay | CPU | When you talk at the same time as someone else |
|---|---|---|---|---|---|
| **Full isolation** (default) | VoxLock neural network | Best quality: removes other voices even while you talk | ~80 ms | Medium | Removed or turned down |
| **Lightweight filter** | VoiceFilter-Lite design (Google) | Laptops too slow for Full isolation | ~70 ms | Low | Turned down |
| **Voice check only** | CAM++ speaker check, no separation | Very slow computers, or rooms where others rarely talk over you | ~70 ms | Very low | Passed with your voice |

All of them mute other people while you are silent. If the computer is too slow for the selected model, VoiceStudio uses
*Voice check only* by itself and says so.

The **Lightweight filter** needs a trained model file. Google did not publish one, so it shows *Needs model file* until one
is trained with `tools\voicefilter` (see its README) and placed in `models\voicefilter\`.

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
  The whole delay - microphone to call app - always stays **under 0.3 seconds**; Setup → **Delay** shows it.
* Isolation runs where it is fastest. A graphics card is not always faster: if the one you choose is slower than
  your CPU, VoiceStudio uses the CPU and says so.
* **Safety first:** while isolation is on, VoiceStudio never sends your raw microphone. If isolation cannot run (for example right after starting, or without a recorded voice), your microphone is **silenced** and a yellow warning explains why.
* If you switch to a **different microphone**, VoiceStudio reminds you to record your voice again.
* **Add more** or **Start over** any time. **Delete** removes the recording from your computer.

---

## 5. Sound settings

* **Voice changer** (Voice page; the voices are also on Home under *Sound like*): click a voice - **Natural**, **Strong & Thick**,
  **Deep**, **Warm**, **Radio**, **Clear**, **Lighter** or **Very Deep** - and it is used right away. The **Voice effects** switch
  turns them off (your natural voice) and back on. Adjust the voice under *Adjust this voice*:
  * **Pitch**: a lower (or higher) pitch. Your voice keeps its own tone, so it sounds deeper and stronger, not robotic.
  * **Thickness**: a thicker, bigger voice. Move it left for a lighter voice.
  * **Presence**: keeps a deeper voice clear and forward.

  **Save as my voice** keeps the current voice under a name; it then appears after the built-in voices. Hover a saved
  voice and click the bin to delete it.

  A lower voice would normally pile up bass and sound boomy; VoiceStudio holds the bass back automatically (more the deeper you go),
  so it sounds strong and thick instead. For strong and thick: 1 to 2 steps of Pitch and some Thickness (that is *Strong & Thick*).
  Voice character adds about 35 ms of delay while it is on, and none when it is set to Natural.
* **Background noise** (Home): *Off*, *Light*, *Balanced* or *Strong*. *Balanced* suits most rooms. Details are on Clean-up.
* **Clicks and taps** (Clean-up → Advanced noise settings, experimental, off by default): turns down mouse clicks
  and keys between your words. It catches about half of them and can slightly soften some consonants.
* **Situation** (Clean-up): ready-made noise and tone settings such as **Interview**, **Natural Voice**, **Gaming** and **Streaming**. Your voice character is kept when you switch.
* **Steady voice at any distance** (Home or Voice): on by default. Leaning in makes a voice louder and boomier,
  sitting back makes it quieter and thinner - and the voice character would then sound different each time.
  The stabilizer evens out your level and that low end before the voice effects, so you sound the same at any distance.
  The two boxes under the switch show what it is correcting right now.
* **Microphone volume** (Home): aim for the microphone meter to read **Good** when you speak normally.
* **Mute**: **Mute microphone** in the sidebar, right-click the tray icon → **Mute microphone**, or press
  **Ctrl+Alt+Shift+M** anywhere (see *Keyboard shortcuts* below).

### Check how you sound

On **Home** or **Voice**, click **Record 5 s and play back**. VoiceStudio plays back exactly what other people hear.

---

## 6. Running in the background

* **Closing the window does not stop VoiceStudio.** It keeps cleaning your voice and lives in the notification area next to the clock. You may need to click **^** to see the icon. Drag it onto the taskbar to keep it visible.
* **Click** the icon to open the window. **Right-click** it to mute, switch strict isolation on or off, or **Quit**.
* **Setup → Start with Windows** starts VoiceStudio automatically (hidden in the tray) when you sign in.
* **Setup → Keep running when I close the window**: turn it off if you prefer that closing the window quits.

### Keyboard shortcuts

They work everywhere - in a game, in the call app, or while VoiceStudio is hidden in the tray.

| Shortcut (default) | What it does |
|---|---|
| **Ctrl+Alt+Shift+V** | Shows the VoiceStudio window; press it again to hide the window (VoiceStudio keeps running in the tray) |
| **Ctrl+Alt+Shift+M** | Mutes the microphone, or unmutes it again |
| **Ctrl+Alt+Shift+N** | Switches to the next voice (built-in voices, then your saved ones) |
| **Ctrl+Alt+Shift+B** | Turns the voice effects off (natural voice), or back on to the last voice |

To change one, open **Setup → Keyboard shortcuts**, click the keys and press the new combination (Esc cancels).
Use Ctrl, Alt or Win together with a letter, number or F-key, so the keys keep working for typing in other apps.
**Off** turns a shortcut off; **Default** brings the original keys back. If another app already uses the keys,
the card says so - choose other keys. The tray menu shows the current shortcuts next to its entries.

### Updates

A few seconds after it starts, VoiceStudio checks GitHub for a newer version. If there is one, it asks you first
and shows what's new - choose **Update now**, **Later**, or **Skip this version**. (If VoiceStudio started hidden in
the tray, a notification tells you; click it.)

**Update now** downloads the installer, checks that it is exactly the file published on GitHub, and installs it.
Windows asks for permission; VoiceStudio closes for a moment and starts again by itself. Your settings, shortcuts
and recorded voice are kept. After the update, VoiceStudio shows what changed.

**Setup → Updates**: check by hand with **Check now**, or turn the automatic check off. The check is the only
connection VoiceStudio makes to the internet.

---

## 7. Troubleshooting

**People in the call can't hear me**

1. In the call app the microphone must be **CABLE Output** (not your real microphone, not "CABLE Input").
2. In VoiceStudio → **Setup**: the green line must say **Sending your voice to "CABLE Input"** (or your own virtual microphone).
3. VoiceStudio must be running (tray icon visible) and not muted.
4. Click **Test virtual microphone** on the Setup page.

**There is a yellow "microphone is silenced" warning.**
Strict voice isolation is on but cannot run yet. Record your voice, or wait a few seconds after starting. If it says isolation is unavailable, switch strict isolation off.

**The other person's voice still comes through.**
See *What to expect* in section 4. Move the microphone closer to you, move others away, and record your voice again in a quiet room with the microphone you use for calls.

**My voice sounds choppy or has small gaps.**
Close heavy programs (games, video editing). VoiceStudio adjusts itself if the computer is too busy. Under **Setup → Advanced → Speed vs. stability**, choose a more stable option.

**"CABLE Output" / "CABLE Input" does not appear.**
Restart Windows after installing. Still missing? Run the setup again with **Install VB-CABLE** ticked, or install it by hand from vb-audio.com/Cable.

**I hear an echo of myself.**
Turn off **Listen live**, or use headphones instead of speakers.

**After unplugging a headset, the microphone stopped.**
VoiceStudio reconnects automatically. If it does not, open **Setup** and click your microphone again.

**Still stuck?**
In **Setup → Help and troubleshooting**, click **Copy diagnostics** and paste it to whoever helps you. It contains settings and device names only, never audio.

---

## 8. Privacy

* All audio is processed **on your computer** and is never sent anywhere. VoiceStudio does not use the internet.
* The only audio saved to disk is the voice you record on **Only my voice** (card *My voice*): the full recording and the 3-second sample used by strict isolation (`%LOCALAPPDATA%\VoiceStudio\voice`). **Delete** removes both.
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
* **Signalsmith Stretch** pitch and formant shifting by Signalsmith Audio (MIT).
* **ONNX Runtime** by Microsoft (MIT) and **Qt** (LGPL-3.0).
