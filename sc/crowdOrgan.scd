// crowdOrgan.scd
// Simple polyphonic Crowd Organ engine driven by /room/voice/* and camera motion OSC messages.
// Assumes CrowdOrganHost is sending to SuperCollider's language port (57120).

s.waitForBoot {
    // A basic "pipe" voice: 2 detuned saws into a resonant lowpass.
    SynthDef(\crowdPipe, { |out=0, freq=220, amp=0.0, pan=0.0, bright=0.5, gate=0|
        var env = Env.asr(0.01, 1.0, 0.5).kr(gate, doneAction: 2);
        var sig = Saw.ar(freq * [1, 1.01]);
        var filtFreq = bright.linexp(0.0, 1.0, 200.0, 8000.0);
        sig = RLPF.ar(sig, filtFreq, 0.25);
        sig = sig * env * amp;
        Out.ar(out, Pan2.ar(sig, pan));
    }).add;

    s.sync;

    // voiceId -> Synth
    ~crowdVoices = IdentityDictionary.new;

    // Ensure there's a running synth for this voiceId.
    ~ensurePipe = { |vid|
        var synth = ~crowdVoices[vid];
        if (synth.isNil or: { synth.isRunning.not }) {
            synth = Synth(\crowdPipe, [\amp, 0.0, \gate, 0]);
            ~crowdVoices[vid] = synth;
        };
        synth
    };

    // /room/voice/active  voiceId, active(0|1)
    OSCdef(\crowdActive, { |msg, time, addr, recvPort|
        var vid = msg[1].asInteger;
        var active = msg[2].asInteger;

        if (active == 1) {
            ~ensurePipe.(vid);  // make sure a synth exists
        } {
            var synth = ~crowdVoices[vid];
            if (synth.notNil) {
                synth.set(\gate, 0);         // release envelope
                ~crowdVoices.removeAt(vid);   // forget this voice
            };
        };
    }, '/room/voice/active');

    // /room/voice/state  voiceId, x, y, z, size, motion, energy
    OSCdef(\crowdState, { |msg, time, addr, recvPort|
        var vid = msg[1].asInteger;
        var x   = msg[2].asFloat;   // -1..1
        var y   = msg[3].asFloat;   // 0..1
        var z   = msg[4].asFloat;   // 0..1
        var size   = msg[5].asFloat;
        var motion = msg[6].asFloat;
        var energy = msg[7].asFloat;

        var synth = ~ensurePipe.(vid);

        // Map x to pan; blend energy into brightness.
        var pan    = x.clip(-1.0, 1.0);
        var bright = (0.3 + (energy * 0.7)).clip(0.0, 1.0);

        synth.set(
            \pan, pan,
            \bright, bright
        );
    }, '/room/voice/state');

    // /room/voice/note  voiceId, note (MIDI), velocity (0..1)
    OSCdef(\crowdNote, { |msg, time, addr, recvPort|
        var vid  = msg[1].asInteger;
        var note = msg[2].asFloat;
        var vel  = msg[3].asFloat;

        var freq = note.midicps;
        var amp  = vel.linexp(0.0, 1.0, 0.001, 0.3);

        var synth = ~ensurePipe.(vid);
        synth.set(
            \freq, freq,
            \amp, amp,
            \gate, 1     // open gate (or keep it open)
        );
    }, '/room/voice/note');

    // Global motion → e.g. global reverb mix or master filter tilt
    ~globalMotion = 0.0;

    OSCdef(\crowdGlobalMotion, { |msg|
        ~globalMotion = msg[1].asFloat.clip(0, 1);
    }, '/room/global/motion');

    // Camera zones → e.g. modulate FX buses by specific corners
    ~cameraZones = IdentityDictionary.new;

    OSCdef(\crowdCamZones, { |msg|
        var camId = msg[1].asInteger;
        var cols  = msg[2].asInteger;
        var rows  = msg[3].asInteger;
        var num   = cols * rows;

        if (msg.size >= 4 + num) {
            var vals = Array.fill(num, { |i| msg[4 + i].asFloat.clip(0, 1) });
            ~cameraZones[camId] = (cols: cols, rows: rows, vals: vals);
        };
    }, '/room/camera/zones');

    "Crowd Organ OSC listeners up on port %\n"
        .postf(NetAddr.langPort);
};
