# dali-master
Mbed implementation of a Dali master

This is a work in progress.  The main Dali code base was taken from the examples in NXP's app note, dali/docs/AN10760.pdf.
The Dali send/receive functionality is working but I was trying to wrap it in a socket interface and ran out of time.  It's now a simple NTP based wrapper that sends a couple of Dali commands based on the time.

Sending a Dali frame requires setting up the forward_frame and calling dali_send().  The Manchester encoding is handled via a timer.
