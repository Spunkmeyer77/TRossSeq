// Sends the sequencer straight to a MIDI port on the computer (a USB/DIN interface or a drum box
// with USB MIDI), so it works in any host - including ones that don't pass MIDI out of plug-ins.
//
// The audio thread only pushes time-stamped events into a lock-free queue. A high-priority thread
// sends each one when its time comes. Opening/closing the port happens on the message thread.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace trossseq
{
class DirectMidiOut : private juce::Thread
{
public:
    DirectMidiOut() : juce::Thread ("TRoss Seq MIDI out") {}
    ~DirectMidiOut() override { close(); }

    // ---- message thread
    bool open (const juce::MidiDeviceInfo& info)
    {
        close();
        auto dev = juce::MidiOutput::openDevice (info.identifier);
        if (dev == nullptr) { lastError = "Couldn't open " + info.name + " (another app may be using it)"; return false; }
        {
            const juce::ScopedLock sl (portLock);
            port = std::move (dev);
        }
        current = info;
        lastError = {};
        fifo.reset();
        startThread (juce::Thread::Priority::highest);
        opened = true;
        return true;
    }

    void close()
    {
        opened = false;
        signalThreadShouldExit();
        wake.signal();
        stopThread (1000);
        const juce::ScopedLock sl (portLock);
        if (port != nullptr)
        {
            // don't leave notes hanging on the drum box
            for (int ch = 1; ch <= 16; ++ch) port->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
        }
        port.reset();
        current = {};
    }

    bool isOpen() const                         { return opened.load(); }   // safe from any thread
    const juce::MidiDeviceInfo& device() const  { return current; }
    juce::String getLastError() const           { return lastError; }

    // ---- audio thread: queue a 1..3 byte message to go out at `timeMs` (Time::getMillisecondCounterHiRes)
    void push (const juce::MidiMessage& m, double timeMs)
    {
        if (! isOpen() || m.getRawDataSize() > 3) return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0) return;   // queue full: drop
        auto& e = events[(size_t) (n1 > 0 ? s1 : s2)];
        e.time = timeMs;
        e.size = m.getRawDataSize();
        std::memcpy (e.bytes, m.getRawData(), (size_t) e.size);
        fifo.finishedWrite (1);
        wake.signal();
    }

private:
    struct Event { double time = 0; int size = 0; juce::uint8 bytes[3] {}; };
    static constexpr int kQueue = 4096;
    juce::AbstractFifo fifo { kQueue };
    std::array<Event, kQueue> events;
    std::vector<Event> waiting;   // read from the queue, not due yet (dispatcher thread only)

    juce::CriticalSection portLock;
    std::unique_ptr<juce::MidiOutput> port;
    juce::MidiDeviceInfo current;   // message thread only
    std::atomic<bool> opened { false };
    juce::String lastError;
    juce::WaitableEvent wake;

    void run() override
    {
        waiting.reserve (kQueue);
        while (! threadShouldExit())
        {
            // move everything queued into `waiting`, keeping time order
            int s1, n1, s2, n2;
            fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) insert (events[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) insert (events[(size_t) (s2 + i)]);
            fifo.finishedRead (n1 + n2);

            const double now = juce::Time::getMillisecondCounterHiRes();
            size_t sent = 0;
            {
                const juce::ScopedLock sl (portLock);
                for (; sent < waiting.size() && waiting[sent].time <= now + 0.25; ++sent)
                    if (port != nullptr)
                        port->sendMessageNow (juce::MidiMessage (waiting[sent].bytes, waiting[sent].size));
            }
            waiting.erase (waiting.begin(), waiting.begin() + (std::ptrdiff_t) sent);

            // sleep until the next event is due (or something new arrives)
            const double wait = waiting.empty() ? 50.0 : waiting.front().time - juce::Time::getMillisecondCounterHiRes();
            if (wait > 1.5)      wake.wait ((int) juce::jmin (50.0, wait - 1.0));
            else if (wait > 0.2) juce::Thread::yield();
        }
    }

    void insert (const Event& e)
    {
        auto it = std::upper_bound (waiting.begin(), waiting.end(), e.time,
                                    [] (double t, const Event& x) { return t < x.time; });
        waiting.insert (it, e);
    }
};
} // namespace trossseq
