# Problem memo -- <team name> (Aaron Seifen, Hazel Daigneault)
## The user
A person or place, named or nameable. Who will this sit next to?

This could sit next to any low-/mid-budget audio engineer's system. This includes myself (Hazel), a lot of my Colorado engineer friends, and plenty of DIY musicians and sound system owners worldwide who don't have $200-500 to shell out for an industry-standard signal processor.

Furthermore, signal processing and modeling/logging can be applied to many fields outside of audio engineering. Examples include but are not limited to research, medicine, and hands-on work.

## The problem
What goes wrong, how often, and what it costs (money, worry,
ruined batches, missed warnings). Observable, not hypothetical.

Crews with hand-built sound systems, especially those where the care went more into the creation and attention to detail than the budget, are often faced with difficulties acquiring cromulent equipment at a reasonable price. In live sound and audio engineering as a whole, the price one pays for their product is often reflective of the quality they will receive.

This leaves sound system owners at an impasse: do they save money while risking sound fidelity and equipment safety, or do they pull money away from other necessities (amplifiers, cabling, etc.) to add a worthy processor? While systems can be tuned extremely well by hand with analog processing and proper deployment, many (if not most) engineers are not capable or invested enough to push a fully analog rig to its potential. However, every dollar counts in a sound system, so pulling money away from basic construction or interconnections can also reduce system caliber and safety.

Those who can't/don't tune systems well without at least implementing decent signal processing often kill events. People have no interest in dancing to Walmart-speaker quality music in a grimey tunnel. However, those who prioritize processing over component and construction quality often have more critical bottlenecks down the line, such as cable gauge or speaker fuses being insufficient, which can lead to fires or driver death.

To make things worse, sound system errors are rarely one-and-done; a poorly processed or configured system often runs that way throughout the night, with constant changes that don't really do anything for anyone. Any sort of construction shortcut or engineering failure will become incredibly apparent in front of whoever decided to come to the event, degrading both public and professional images at once.

## Why a device
The 3 a.m. test: why must something be physically present and
always awake? Why doesn’t a phone app already solve this?

The main focus of an audio engineer at 3 AM after 6 hours of raving with no break shouldn't be to manage driver/amplifier safety or maintain basic soundcheck quality. Instead, engineers should focus on system stability and keeping an event running smoothly throughout the night by interacting with the system as a musical instrument with utility, not a utility/tool that makes music. Phone apps can't solve this without dedicated, expensive hardware, as the market is niche and evolving enough to warrant specialty pricing on anything worthy of consideration. Thus, companies spend a lot of time creating one-size-fits-most monolith hardware which can manage an entire system, rather than smaller, more focused processors.

## The sensors
Which two (or more), and how they COOPERATE (fused, correlated,
or one pipeline) rather than merely coexist.

One XLR combo jack input (3-pin XLR + 1/4" TRS) to be taken from the mixing desk monitors and collects what the system *should* sound like. This is the reference or "Source Signal", and it will be compared against the "System Signal" later on. 

One 3-pin XLR input takes external input from a microphone (ideally a real-time analysis microphone, but doable with anything decent enough to produce a flat signal) to use as what the system *does* sound like. This is the real-world system output (or "System Signal") as it (more or less) would be perceived by a listener.

Note: XLR = model X - Latching - with Resilient rubber connector surrounds, TRS = Tip-Ring-Sleeve. These are standard audio connectors used for balanced (and, secondarily, unbalanced) interconnects. Balanced connections usually carry two phase-inverted copies of the signal with a ground pin to allow the receiver to reject common-mode noise applied to both copies.

The signal processor focuses on aligning the Source and System Signals and adding some light system protection. For this assignment, the processor will likely contain equalization (EQ) and dynamic range control (compression, expansion, gating, limiting) to be immediately applicable. The Source Signal is the reference for which the System Signal will compensate, and so the desk input (sensor 1) acts as a control monitor for the microphone input (sensor 2), thus cooperating in an example/learner relationship.

## The mechanisms
First guess at two items from the Section 3.2 menu, one sentence
of justification each. Allowed to change by the design doc.

Multi-process architecture where each sensor runs separately while sharing information to manage both source signal normalization and system signal correction concurrently.

High sampling rate with a no-drop guarantee would prevent noise, signal corruption, and latency spikes due to blocking.

## The risk
The single thing most likely to sink this project. Name it now;
it is cheaper to meet in Week 5 than in Week 14.

Ensuring low latency between source input and system output will likely be a difficult task due to the optimization and system strength/reliability required to maintain it.