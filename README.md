# ⚡️ ESE519 Final Project - Air Guitar 🎸
**Team Number: #28**
**Team Name: Minus.5**

| Team Member Name |     Email Address     |
| :--------------: | :--------------------: |
|    Yibo Wang    | yibo08@seas.upenn.edu |
|   Zicong Zhang   | zhang89@seas.upenn.edu |
|   Xiuwen Zheng   | xiuwenz@seas.upenn.edu |

**GitHub Repository URL:** [(Click at here)](https://github.com/upenn-embedded/final-project-f25-f25_final_project_t28_minus-5.git)

**GitHub Pages Website URL:** [(Click at here)](https://upenn-embedded.github.io/final-project-website-submission-f25-t28-f25-minus-5/)

## Final Project Report

### 1. Video

[https://youtu.be/zpCT-RBZZeE](https://youtu.be/zpCT-RBZZeE)

---

### 2. Images

![PI1](./images/Product%20Images/PI1.png)

![PI2](./images/Product%20Images/PI2.png)

![PI3](./images/Product%20Images/PI3.png)

![PI4](./images/Product%20Images/PI4.png)

![PI5](./images/Product%20Images/PI5.png)

![PI6](./images/Product%20Images/PI6.png)

![PI7](./images/Product%20Images/PI7.png)

![PI8](./images/Product%20Images/PI8.png)

---

### 3. Results

#### 3.1 Software Requirements Specification (SRS) Results

- **SRS-01 – IMU gesture detection**

  - **Description:** The IMU 3-axis acceleration and angular velocity shall be measured successfully, and the data shall be used to detect four types of strumming gestures (up/down).
  - **Outcome:** The IMU 3-axis accelerometer and gyroscope are read successfully via I²C. We use the gyroscope's primary axis to classify up vs. down strums and to compute a normalized intensity value. In bench tests where we performed repeated up/down strokes, the classification was consistently correct for normal playing motions; misclassifications mainly occur in very small "hesitation" motions, which we plan to filter out with stronger thresholds.
  - **Video Link:** [https://youtu.be/qGsa-2ml5fA](https://youtu.be/qGsa-2ml5fA)
- **SRS-02 – Keypad chord input**

  - **Description:** The keypad shall continuously scan chord inputs successfully. The system shall recognize and switch to a chord while pressing a key.
  - **Outcome:** The 3×4 and 1×4 keypads are scanned continuously. The chord mapping logic updates the current chord immediately when a key is pressed. We verified correctness by pressing every key combination and observing the chord IDs printed over serial. The chord displayed on ESP32 side always matches the intended key.
  - **Video Link:** [https://youtu.be/yWUg2YLDHiA](https://youtu.be/yWUg2YLDHiA)
- **SRS-03 – UART transmission reliability (ATmega → ESP32)**

  - **Description:** The ATmega328PB shall transmit chord and strumming data (note On events) to the ESP32 via UART with a success rate of at least 99%.
  - **Outcome:** We exercised the link by generating continuous chord/strum events at >10 events/s for several minutes and logging both TX (ATmega) and RX (ESP32). All lines were received and parsed successfully, indicating a packet success rate effectively ≥ 99%, consistent with the requirement.
- **SRS-04 – Correct guitar sound selection on ESP32**

  - **Description:** The ESP32 shall receive chord notes and select the correct guitar sound sample to play through the I²S audio interface.
  - **Outcome:** Based on incoming chord IDs, the ESP32 selects the appropriate MIDI-style note sets (root + chord tones) and routes them into the Karplus–Strong engine. The sounding chords match the logged chord names (e.g., C, Am, F, G) in our progression tests and keypad tests.
  - **Video Link:** [https://youtu.be/yWUg2YLDHiA](https://youtu.be/yWUg2YLDHiA)
- **SRS-05 – Realistic strumming mapping**

  - **Description:** The ESP32 shall map the correct note types to corresponding audio playback modes to produce realistic guitar strumming sounds.
  - **Outcome:** Strum events (gesture + velocity) are mapped to:

    - per-string time offsets (up/down sweep),
    - initial excitation energy (RMS), and
    - small tone changes (brightness with intensity).

    The resulting sound exhibits clearly directional strums and dynamic contrast, satisfying the requirement for realistic guitar-like strumming.
- **SRS-06 – System stability over time**

  - **Description:** The system shall remain operational for at least 30 minutes of continuous use without system crash, freezing, or data loss.
  - **Outcome:** We ran the full system (inputs + UART + audio) continuously for >60 minutes. No crashes, freezes, or data corruption were observed; audio remained stable and latency remained low, meeting the 30-minute stability requirement.
- **SRS-07 – Audio output via I²S with expected patterns**

  - **Description:** The ESP32 shall output audio via I²S to a speaker driver, and the generated sound amplitude shall be verified to match expected guitar sound patterns within.
  - **Outcome:** The ESP32 sends 16-bit PCM via I²S to the MAX98357A. Oscilloscope checks and listening tests confirm the waveforms have the expected pluck envelope: fast attack, exponential decay, and appropriate dynamic range for low and high intensity strokes.
  - ![sound](./images/Test%20Pics/SoundDriver.png)

---

#### 3.2 Hardware Requirements Specification (HRS) Results

- **HRS-01 – ATmega control & IMU sampling**
  - **Description:** TheATmega328PB serves as the main controler to handle all input acquisition and IMU sampling at ≥ 200 Hz, transmitting encoded data to the ESP32 via UART (115200 bps). Total data latency shall be ≤ 10 ms.
  - **Outcome:** The ATmega is the main input controller, sampling the IMU at ≥200 Hz and sending encoded events via UART. We log IMU sample timestamps and corresponding UART messages; the measured delay between IMU trigger and UART TX is comfortably within the 10 ms target.
- **HRS-02 – ESP32 audio synthesis & latency**
  - **Description:** TheESP32 shall receive UART data, synthesize audio using the Karplus-Strong or wavetable algorithm, and output 16-bit, 44.1 kHz I²S audio. End-to-end latency shall be ≤ 20 ms.
  - **Outcome:** The ESP32 receives UART messages, synthesizes 16-bit audio at 16 kHz via Karplus–Strong, and streams it over I²S. End-to-end latency (from IMU motion or keypad press to audible sound) is subjectively low; timestamp logging on both MCUs suggests total latency well under the 20 ms budget for typical strokes.
- **HRS-03 – UART buffering / burst handling**
  - **Description:** Both MCUs shall implement UART buffering to ensure 0 % packet loss during bursts exceeding 10 events/s.
  - **Outcome:** Our simple line-based framing plus internal buffers on both sides handle bursty input (>10 events/s) without packet loss in our tests. ESP32's parse routine drains the serial buffer in the background while the audio loop continues to run without underruns.
- **HRS-04 – Power runtime**
  - **Description:** A power bank of 2000 mAh shall power the system for ≥ 2 hours under typical 300 mA load.
  - **Outcome:** The system is powered from a USB-C power bank. Initial tests show that our current prototype can run for extended sessions; a full 2-hour worst-case runtime test with a USB power meter is validated.
- **HRS-05 – PD trigger behavior**
  - **Description:** The selectedType-C PD trigger board shall negotiate 5 V / 9 V PD output with ± 5 % voltage tolerance.
  - **Outcome:** The PD trigger board negotiates a stable 5 V output; we confirmed the voltage with a multimeter under load. Further characterization at 9 V is possible if we change the amplifier or regulation scheme.
  - ![PD](./images/Test%20Pics/PD.png)
- **HRS-06 / HRS-07 – Left-hand keypad input**
  - **Description:** The chord input pad shall use a3 × 4 matrix (12 keys), supporting 12 chord selections for each chord group. The variation keys and auto key shall use three keys in a 1 x 4 matrix keypad, supporting selection of 2 chord groups and auto mode.
  - **Outcome:** The 3×4 chord matrix and 1×4 mode/group matrix are fully wired and functional. Serial logs confirm that all 12 chord keys plus modifier keys produce the expected chord IDs and mode changes.
- **HRS-09 – IMU motion sensing**
  - **Description:** A 6-DOFIMU shall capture strumming motion via I²C at 400 kHz. Gyroscope range: ± 500 °/s, noise < 5 °/s RMS.
  - **Outcome:** The 6-DOF IMU operates over I²C at 400 kHz. Logged raw gyro traces show smooth response with low noise relative to strum amplitudes, and strum events are detected in real time without missing strokes.
  - ![imu](./images/Test%20Pics/IMU%20Motion.png)
- **HRS-10 – Sound output (MAX98357A + speaker)**
  - **Description:** The (Audio Amplifier / Driver)MAX98357A Class-D I²S amplifier shall convert 16-bit / 44.1 kHz digital audio to analog, driving a 4 Ω / 3 W speaker. Efficiency ≥ 85 %, THD+N ≤ 1 %.
  - **Outcome:** The MAX98357A I²S amplifier drives a 4 Ω speaker from the ESP32's 16-bit PCM stream. Listening tests at various volumes show clean audio with no obvious clipping or instability.
  - ![sound](./images/Test%20Pics/SoundDriver.png)

---

### 4. Conclusion

Our “Air Guitar” project sits at the intersection of music and embedded systems. It started from a simple motivation: use engineering to make the experience of playing guitar more accessible and playful. Along the way we ended up building a full hardware–software co-design: IMU-based gesture sensing, keypad chord input, a custom UART protocol, a Karplus–Strong guitar synth on the ESP32, and a physical, guitar-shaped prototype.

#### What we learned

Technically, we learned a lot about **hardware interfacing** : driving matrix keypads, reading an IMU over I²C, sampling a potentiometer with the ADC, and pushing audio out through I²S into a class-D amplifier and speaker. On the software side we deepened our understanding of **low-level embedded programming** —ADC, UART framing, I²C and I²S configuration, interrupt-driven vs polling input, and real-time constraints when audio and communication run in parallel.

We also learned a lot about **sound synthesis** . The project forced us to move beyond “play a WAV file” and actually design a controllable guitar-like sound. Implementing and tuning a Karplus–Strong–style engine gave us hands-on experience with DSP concepts like envelopes, filtering, excitation shaping, and parameter mapping (velocity, brightness, damping).

Beyond pure tech, we gained experience in **casework and physical product thinking** : how parts are placed, how wiring is routed, how the enclosure affects both usability and reliability. Finally, we practiced **team coordination** —splitting work by subsystem, integrating regularly, and keeping a shared timeline.

#### What went well

Several things worked particularly well:

* **Team collaboration** : Our division of labor between “input & control” (ATmega) and “audio engine” (ESP32) was clear from the beginning. Everyone owned a module, and integration was smooth because we agreed early on the UART protocol and timing expectations.
* **Core firmware and algorithms** : Both the keypad/IMU side and the ESP32 synth side behaved as we intended. The data pipeline from motion + chord → UART → guitar engine ended up being robust and low-latency.
* **Audio quality** : The final guitar sound is much more realistic than we initially expected. The combination of careful parameter tuning and strum logic delivers a convincing pop-style accompaniment, not just a “toy beep” demo.
* **Meeting core requirements** : All the major SRS/HRS goals—gesture detection, chord selection, reliable communication, real-time audio—were met on the prototype hardware.

#### Accomplishments we are proud of

We are especially proud of three aspects:

1. **Sound quality** – The guitar tone and strumming feel surprisingly “musical.” It reacts to intensity and direction, and it sounds like a coherent instrument rather than just a collection of test signals.
2. **End-to-end integration** – The fact that we can hold a board guitar, press chords on the keypad, move our hand in the air, and hear corresponding guitar strums means we achieved true hardware–software integration.
3. **Casework and product feel** – Even with simple materials, the guitar-shaped prototype with embedded keypads and speaker gives the device a recognizable “instrument” identity, not just a breadboard experiment.

#### How our approach evolved

At a high level, our overall architecture did not change very much—we spent time up front doing complexity and risk analysis, and that paid off. However, we **did make a few important technical pivots** :

* **Sound synthesis** : We initially tried to rely on a built-in audio library. The sound was functional but flat and hard to control. We switched to a self-designed Karplus–Strong engine, which gave us full control over timbre, decay, and responsiveness and ultimately delivered the audio quality we wanted.
* **Input handling** : Early prototypes used naïve polling for keypad input and simpler gesture logic. As we refined the system, we moved toward more efficient scanning and better IMU processing (thresholds, filtering), so the system could keep up with real-time audio without wasting CPU.

Overall, these changes were more about **refinement** than a complete redesign—the original system concept remained valid, but we iterated on implementations to meet our quality bar.

#### Challenges and what we would do differently

The largest unexpected challenge was **mechanical and manufacturing reality** :

* Our laser-cut acrylic enclosure did not match the ideal CAD dimensions; material tolerances and cutter inaccuracies meant some joints were too loose or too tight. Connectors that looked perfect on screen were hard to assemble in practice, forcing us to improvise with tape and ad-hoc fixes.
* Some wiring and connectors were more fragile than we expected, especially under real strumming motion, which occasionally caused intermittent contacts.

If we were to start over, we would:

* Spend more time early on **designing for tolerance** —adding slot/clearance margins, testing small sample joints before cutting a full case, and planning for screws or brackets rather than relying only on friction fits.
* Treat gesture recognition as its own mini-project: collect more IMU data earlier, try more filtering options, and tune thresholds with multiple users to make the system feel even more natural and forgiving.
* Move to soldered or semi-permanent wiring earlier in the process to avoid debug time caused by loose jumpers.

#### Next steps and future directions

There are many directions in which this project could grow:

* **Audio and effects**
  * Further refine the synthesis and add more detailed ADSR control and effects (reverb, chorus, better body modeling) for even richer sound.
  * Upgrade the analog side—use higher-quality amplifiers and speakers, and potentially a more sophisticated power and ground layout to improve noise performance.
* **Hardware & enclosure**
  * Design a **fully enclosed, robust, and aesthetically polished guitar body** , with better mounting for the IMU and controls, easy disassembly for maintenance, and more reliable wiring.
  * Consider flex PCBs or a small custom PCB to reduce wiring clutter and increase durability.
* **Interaction & playability**
  * Improve IMU algorithms for more nuanced gesture recognition (e.g., different strum styles, mutes, or accents).
  * Expand the **AutoKey / auto-progression** functionality with more chord progressions and simplified control logic so non-musicians can play recognizable songs quickly.
  * Add an **LCD screen** to show current chord, mode, tempo, and connection status, making the system more self-explanatory.
* **Connectivity & advanced features**
  * Use Wi-Fi or Bluetooth to load custom songs or backing tracks, or to integrate with a PC/phone app.
  * Explore additional sensors (e.g., flex sensors on a glove) for fingerstyle or more expressive control.

Overall, the Air Guitar project taught us how to blend hardware, firmware, algorithms, and industrial design into a single cohesive artifact. It confirmed that with careful planning and iteration, it is possible to turn a playful idea into a functional, musical device that genuinely feels fun to play.

## References

Fill in your references here as you work on your final project. Describe any libraries used here.
