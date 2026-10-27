# CS-370 Promptlog/Devlog
## 10/16 [Hazel]
- Pointed Claude to component list, problem file, and CLAUDE rules. Asked for a walkthrough of the OS structure to be implemented.
- Asked about process naming convention clarifications, context on when proposed analyzer process switches buffers, full-system delay to compensate for latency, and audio/analyzer process restarts on failure. Clarified gaps regarding power supplies and dynamic vs. condenser requirement.
- Decided to use duplicate-mono stereo out due to single-ADC setup; adding a second input would allow for stereo in/out in a later model.
- Logged proposed structure in ProposedStructure.md.

## 10/23 [Hazel]
- Pointed Claude to proposed structure and asked for a walkthrough of the OS configuration. Discovered that the Pi doesn't have a native real-time kernel, so I pointed it to a copy-paste of a blog post and let it Google stuff. Took the usage hit for the sake of understanding how to implement PREEMPT_RT, since the forum website are godawful on the eyes and the information was nested multiple references deep. Decided to install an experimental RT build which people have had neutral-to-positive experiences with. For low-latency GPIO, the experimental risk is worth it.
- Update the firewall to accept LAN key-only SSH for deploy and port inputs only with no outbound traffic. Fumbled the first iteration because I misunderstood the header.
- Also directed away from PROMPTLOG. None of its business.
- Proposed structure updates now include data regarding expected latency with respect to sample rate and frame periods. Additionally, RBP OS stuff was included in the proposed repo to prevent updating RBP stuff on the Pi itself.
- Included tests specifically to ensure real-time kernel usage, plus proper watchdog monitoring.
- Updated repo.

## 10/26 [Hazel]
- Pointed Claude to proposed structure and asked for help making an architecture diagram. Clarified specific rates, buffer quantities, and EQ specifications. Clarified diagram type (also to exclude the section 3.2 bit from the sample diagram lol) and pushed to include a legend. Moving to failure-mode table.
- Updated ownership in CLAUDE.md.