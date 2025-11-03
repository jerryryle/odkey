# About the ODKey

You can find technical information and instructions for ODKey use in [README.md](/README.md).

I bought a 3D printer earlier this year and I've been printing random things for my coworkers. Usually, they'll find a model on a site like [makerworld](https://makerworld.com) and send it to me for printing. It doesn't take much effort on my part to print these & it's helped me learn more about the printer and 3D printing in general.

Near the end of September, I announced in Slack:
> Me: My 3D printer will be idle for a bit, if anyone wants something printed. I promise not to put teeth in it, unless you ask.

The teeth thing is a running joke from a previous prank. Ignore that. What's important is that one of my coworkers, OD, replied:
>OD: Can I get a single functioning keycap the size of a normal macbook keyboard? Feel free to add teeth to it.

The context here is that giant keyboards had become a meme at work, with people bringing in [ridiculously-large keyboards](https://www.amazon.com/Redragon-Mechanical-Keyboard-Ergonomic-Anti-Ghosting/dp/B09V4HTWLT) and holding typing contests.

>Me: You want one big keycap to cover your MacBook keyboard? And what does "functional" entail here?

>OD: It can be pressed. Not to cover, just the size of it. No need to send input, just be physically clicky like one of [other coworker]'s giant keys

>Me: Oh. I see. Hm unless you can find an existing design online, that requires Jerry’s Design Services, which are a scarce resource.

>OD: I think it should be possible to just scale up a mx keycap stl to have the stem slots fit a kailh giant switch, a spacebar with multiple stem slots scaled up to that amount would probably be the easiest

>Me: Hmm. We’ll see if Jerry’s Design Services get bored enough to try that. If they are, what would the key silkscreen be? Spacebar (blank)? Letter/number? Etc.

>OD: I think it could be blank, or if Jerry's design services are amused to, maybe [our company name] or something like that on a space bar

Now, I generally don't target individuals with pranks, but I felt that this coworker had just opted himself into one. And Jerry's Design Services did get very bored, so I decided to take on his request...with some modifications that would be more fun for me.

First, the key would be large, but not the size of a MacBook keyboard. That would have required more mechanical design work than I cared to take on and would also take much longer to print. So, I decided I would create a more manageable 100mm x 100mm x 65mm keycap. This would be comically large, but wouldn't require more mechanical stabilization than an off-the-shelf switch could provide.

Second, the key would be functional--not just in that it could be pressed and make a click sound. It would enumerate as a USB HID keyboard and, when you pushed the button, it would emit key presses. I decided to implement a rudimentary scripting language so that the button's function could be updated. By default, it would type my coworker's initials, "OD"; hence, I would call it the "ODKey."

Third, and this was very important, it would contain a WiFi radio and HTTP interface that would let me send keypresses of my choosing to whatever computer the ODKey is plugged into.

To begin, I sourced a large mechanical key because I didn't want to create the switch mechanism from scratch. I found the [NovelKeys Big Switch series](https://novelkeys.com/collections/switches/products/the-big-switch-series?variant=3747938500648) and purchased the [blue Clicky](https://www.adafruit.com/product/5307) model from Adafruit.

![The Blue Big Switch](media/img-blue-key.jpeg "Photo of a blue Big Switch next to the box it came in")

Next, I needed a microcontroller that had both USB device and WiFi functionality. The [Espressif ESP32-S2](https://www.espressif.com/en/products/socs/esp32-s2) has both. Adafruit offers the [ESP32-S2 Feather](https://www.adafruit.com/product/5000) which incorporates the ESP32-S2 onto a PCBA with a nice, small form-factor.

![The ESP32-S2 Feather](media/img-feather.jpeg "Photo of an ESP32-S2 PCBA on my desk")

I took measurements from the blue keycap that came with the Big Switch and roughly used these to model my own keycap, scaling it up about 1.4x.

![Keycap](media/3d-keycap.png "Screenshot of a 3D CAD model of the keycap")

This was an early test print I did to ensure I had the geometry and tolerances right for the part of the keycap that would slide over the switch's stem.

![Stem test print](media/img-stem-test.jpeg "Photo of a cross-shaped 3D print")

I then modeled a base that the big key snaps into. The base also holds the ESP32-S2 Feather PCBA. I modeled the base in two parts, with a bottom that I could screw on after assembling the switch and PCBA.

![Base top](media/3d-base-top.png "Screenshot of a 3D CAD model of the top portion of the base")

![Base bottom](media/3d-base-bottom.png "Screenshot of a 3D CAD model of the bottom portion of the base")

I printed everything in matte PLA using my Bambu Labs H2D.

![Keycap print preparation](media/bambu-3dprint-keycap.png "Screenshot of the keycap in Bambu Studio")
[![Keycap printing](media/img-print-keycap.jpeg "Time lapse video of the keycap 3D printing")](https://youtu.be/hejnoI1uLic)

![Base top print preparation](media/bambu-3dprint-base-top.png "Screenshot of the base top in Bambu Studio")
[![Base top printing](media/img-print-base-top.jpeg "Time lapse video of the base top 3D printing")](https://youtu.be/YH3hyE5TDeQ)

![Base bottom print preparation](media/bambu-3dprint-base-bottom.png "Screenshot of the base bottom in Bambu Studio")
[![Base bottom printing](media/img-print-base-bottom.jpeg "Time lapse video of the base bottom 3D printing")](https://youtu.be/WQhLJaSar2g)

I had never tried making a cosmetic model before, so I decided to give it a shot instead of using the raw 3D printed parts. This involved a ton of sanding, priming, sanding, filling, sanding, priming, sanding, and painting. The goal was to turn the raw print:

![Raw keycap 3D Print](media/img-raw-keycap.jpeg "Photo of the raw 3D print of the keycap, with its layer lines from the 3D printing process")

into a nicer-looking version with a refined surface finish and a label:

![Finished keycap 3D Print](media/img-finished-keycap.jpeg "Photo of the finished 3D print of the keycap, with a smooth surface finish and 'OD' key label.")

It took a few tries and I still need to hone my sanding/painting skills more, but I'm pretty happy with how it turned out. Here's the whole thing, freshly assembled.

![Finished ODKey](media/img-finished-key.jpeg "Photo of the finished and fully-assembled ODKey.")

A few key lessons:

* Primer is insufficient to fill deeper cracks. If you try to build up a base of primer that's thick enough to fill them, you'll wind up with a gummy mess when sanding. Use putty to fill deeper cracks. I had watched some videos that warned about this, but I thought the cracks I was filling with primer were small enough to get away with. They were not and I used too much primer.
* If the surface isn't perfectly smooth before painting, imperfections will stand out once painted.
* Paint in very light coats, keeping the can at a distance, in order to prevent drips that look terrible. I wound up with a few small ones. Supposedly you can sand them down and re-paint, but I was tired of sanding and painting by that point.
* Don't leave painted PLA models in direct sunlight to dry! PLA softens at relatively low temperatures. Both halves of my base warped irreparably because I left them in the sun to dry after their first coat of paint. A huge waste of time spent on printing/sanding/priming.
* Don't put too much clear coat on at once. I did this by accident to the first keycap that I had sanded and painted perfectly and destroyed it with a wrinkled texture.

![Wrinkled keycap](media/img-wrinkled-keycap.jpeg "Photo of a keycap that has a wrinkled texture due to too much clear coat")

All the waiting for printing and paint to dry gave me plenty of time to work on the ODKey firmware. I largely vibe-coded it using Cursor, which did reasonably well with supporting Python scripts, though it struggled a bit with the firmware. I began with a simple USB HID device that pressed/released the "A" key 1 second after it was plugged into USB. Cursor got a reasonable framework for this up & running, though it made several mistakes in the USB descriptor that I had to hunt down and fix. And, as I extended the codebase with more and more functionality, I found myself constantly fixing thread-safety issues that Cursor introduced.

I knew that I wanted to use a scripting language for the keypress functionality so that the device could be easily user-reprogrammed. I looked into [DuckyScript™](https://docs.hak5.org/hak5-usb-rubber-ducky/usb-rubber-ducky-by-hak5/), but couldn't be arsed to read the huge license for it. It had way more functionality than I needed anyways, so I decided to roll my own.

I wrote the [ODKeyScript](ODKeyScript.md) specification and told Cursor to generate a Python compiler, a Python disassembler, and a C VM for the language. It did this task surprisingly well. Having the disassembler helped me verify that the compiler was behaving as expected and I found and fixed a few small issues that stemmed from under-specification. The VM that Cursor produced was straightforward and reasonable.

I now have an ODKey hardware device that enumerates as a USB keyboard and runs a pre-programmed ODKeyScript when the button is pushed. I can connect to the ODKey remotely via WiFi and upload/run ODKeyScripts using an HTTP interface protected with an API key. I have created and tested a series of scripts that let me launch Slack and say entertaining things on behalf of the poor soul who has plugged the ODKey into their computer.

This should be fun. And, once fun has been had, I'll point my coworker to this repo so he can see how to reprogram and enjoy his giant macro key.
