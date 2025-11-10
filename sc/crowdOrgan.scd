// crowdOrgan.scd
// Crowd Organ engine that listens for motion + gesture OSC messages.
// Boot the server, evaluate this file, and it will wire OSCdefs for /room/*.
// The comments read like a notebook so performers and coders can remix live.

s.waitForBoot {
    SynthDef(\crowdPipe, { |out=0, freq=220, amp=0.0, pan=0.0, bright=0.5, gate=0, color=0.0, trem=0.0, burst=0.0|
        // One synth voice: blend between a saw stack and a pulse stack, filter it,
        // then modulate with tremolo and burst hits. Every control maps to a
        // gesture knob below so students can trace the signal chain.
        var vib = SinOsc.kr(4 + (trem * 4), 0, trem * 0.02, 1);
        var env = Env.asr(0.01, 1.0, 0.7).kr(gate, doneAction: 2);
        var saws = Saw.ar(freq * vib * [1, 1.01]);
        var reed = Pulse.ar(freq * vib * [1, 1.005], 0.45);
        var blend = XFade2.ar(saws, reed, color.clip(-1, 1));
        var filtFreq = bright.linexp(0.0, 1.0, 200.0, 9000.0);
        var sig = RLPF.ar(blend, filtFreq, 0.18 + (bright * 0.4));
        sig = sig * env * amp * (1.0 + burst.clip(0, 1));
        Out.ar(out, Pan2.ar(sig, pan));
    }).add;

    s.sync;

    ~crowdVoices = IdentityDictionary.new;
    ~voiceState  = IdentityDictionary.new;

    ~registerBright = [0.32, 0.5, 0.72, 0.9];
    ~registerGain   = [0.8, 1.0, 1.25, 1.5];
    ~globalAmpBoost = 1.0;
    ~globalColorOffset = 0.0;

    ~ensurePipe = { |vid|
        var synth = ~crowdVoices[vid];
        if (synth.isNil or: { synth.isRunning.not }) {
            synth = Synth(\crowdPipe, [\amp, 0.0, \gate, 0]);
            ~crowdVoices[vid] = synth;
        };
        synth
    };

    ~ensureVoiceState = { |vid|
        var state = ~voiceState[vid];
        if (state.isNil) {
            state = (register: 1, baseAmp: 0.08, color: 0.0, trem: 0.0, energy: 0.0);
            ~voiceState[vid] = state;
        };
        state
    };

    ~applyVoiceLevels = { |vid|
        var state = ~ensureVoiceState.(vid);
        var reg = state[\register].clip(0, ~registerBright.size - 1);
        var synth = ~ensurePipe.(vid);
        var amp = state[\baseAmp] * ~registerGain[reg];
        amp = amp * (0.7 + (state[\energy] * 0.6));
        amp = (amp * ~globalAmpBoost).clip(0.0, 1.0);
        var color = (state[\color] + ~globalColorOffset).clip(-1.0, 1.0);
        synth.set(
            \bright, ~registerBright[reg],
            \amp, amp,
            \color, color,
            \trem, state[\trem].clip(0.0, 1.0)
        );
    };

    ~applyAllVoices = {
        ~voiceState.keysValuesDo { |vid, state|
            ~applyVoiceLevels.(vid);
        };
    };

    ~scheduleReset = { |time, func|
        AppClock.sched(time, {
            func.value;
            nil;
        });
    };

    // /room/voice/active  voiceId, active(0|1)
    OSCdef(\crowdActive, { |msg|
        var vid = msg[1].asInteger;
        var active = msg[2].asInteger;

        if (active == 1) {
            ~ensurePipe.(vid);
            ~ensureVoiceState.(vid);
            ~applyVoiceLevels.(vid);
        } {
            var synth = ~crowdVoices.removeAt(vid);
            if (synth.notNil) {
                synth.set(\gate, 0);
            };
            ~voiceState.removeAt(vid);
        };
    }, '/room/voice/active');

    // /room/voice/state  voiceId, x, y, z, size, motion, energy
    OSCdef(\crowdState, { |msg|
        var vid = msg[1].asInteger;
        var x   = msg[2].asFloat;
        var energy = msg[7].asFloat;
        var synth = ~ensurePipe.(vid);
        var state = ~ensureVoiceState.(vid);

        state[\energy] = energy.clip(0, 1);
        synth.set(\pan, x.clip(-1, 1));
        ~applyVoiceLevels.(vid);
    }, '/room/voice/state');

    // /room/voice/note  voiceId, note (MIDI), velocity (0..1)
    OSCdef(\crowdNote, { |msg|
        var vid  = msg[1].asInteger;
        var note = msg[2].asFloat;
        var vel  = msg[3].asFloat;

        var freq = note.midicps;
        var baseAmp = vel.linexp(0.0, 1.0, 0.001, 0.2);
        var synth = ~ensurePipe.(vid);
        var state = ~ensureVoiceState.(vid);

        state[\baseAmp] = baseAmp;
        synth.set(\freq, freq, \gate, 1);
        ~applyVoiceLevels.(vid);
    }, '/room/voice/note');

    ~globalMotion = 0.0;
    OSCdef(\crowdGlobalMotion, { |msg|
        ~globalMotion = msg[1].asFloat.clip(0, 1);
    }, '/room/global/motion');

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

    // --- Gesture mapping -------------------------------------------------

    ~gestureHandlers = (
        voice: IdentityDictionary[
            raise: { |vid, strength, extra|
                var state = ~ensureVoiceState.(vid);
                var step = (strength >= 0.6).if(2, 1);
                state[\register] = (state[\register] + step).clip(0, ~registerBright.size - 1);
                ~applyVoiceLevels.(vid);
            },
            lower: { |vid, strength, extra|
                var state = ~ensureVoiceState.(vid);
                var step = (strength >= 0.6).if(2, 1);
                state[\register] = (state[\register] - step).clip(0, ~registerBright.size - 1);
                ~applyVoiceLevels.(vid);
            },
            swipe_left: { |vid, strength|
                var state = ~ensureVoiceState.(vid);
                state[\color] = (state[\color] - (0.4 + strength * 0.4)).clip(-1.0, 1.0);
                ~applyVoiceLevels.(vid);
            },
            swipe_right: { |vid, strength|
                var state = ~ensureVoiceState.(vid);
                state[\color] = (state[\color] + (0.4 + strength * 0.4)).clip(-1.0, 1.0);
                ~applyVoiceLevels.(vid);
            },
            shake: { |vid, strength|
                var state = ~ensureVoiceState.(vid);
                state[\trem] = (state[\trem] + (strength * 0.6)).clip(0, 1);
                ~applyVoiceLevels.(vid);
                ~scheduleReset.(1.2, {
                    state[\trem] = state[\trem] * 0.4;
                    ~applyVoiceLevels.(vid);
                });
            },
            burst: { |vid, strength|
                var synth = ~ensurePipe.(vid);
                var boost = strength.linexp(0.0, 1.0, 0.0, 0.8);
                synth.set(\burst, boost);
                ~scheduleReset.(0.4, { synth.set(\burst, 0.0); });
            },
            hold: { |vid, strength, extra|
                var state = ~ensureVoiceState.(vid);
                state[\trem] = state[\trem] * 0.2;
                state[\register] = (state[\register] - 1).clip(0, ~registerBright.size - 1);
                state[\color] = (state[\color] * 0.5);
                ~applyVoiceLevels.(vid);
            }
        ],
        zone: IdentityDictionary[
            pulse_zone: { |camId, strength, zoneIndex|
                ~globalAmpBoost = (1.0 + (strength * 0.5)).clip(1.0, 1.8);
                ~applyAllVoices.();
                ~scheduleReset.(0.7, {
                    ~globalAmpBoost = 1.0;
                    ~applyAllVoices.();
                });
            },
            sweep: { |camId, type, strength|
                if (type.contains("lr")) {
                    ~globalColorOffset = (strength * 0.6);
                } {
                    if (type.contains("rl")) {
                        ~globalColorOffset = -(strength * 0.6);
                    } {
                        if (type.contains("tb")) {
                            ~globalAmpBoost = (1.0 + strength * 0.35).clip(1.0, 1.6);
                        } {
                            ~globalAmpBoost = (1.0 - strength * 0.25).clip(0.6, 1.0);
                        };
                    };
                };
                ~applyAllVoices.();
                ~scheduleReset.(1.5, {
                    ~globalColorOffset = 0.0;
                    ~globalAmpBoost = 1.0;
                    ~applyAllVoices.();
                });
            }
        ],
        global: IdentityDictionary[
            eruption: { |strength|
                var boost = (1.3 + strength * 0.6).clip(1.0, 2.0);
                ~globalAmpBoost = boost;
                ~voiceState.keysValuesDo { |vid, state|
                    state[\register] = (state[\register] + 1).clip(0, ~registerBright.size - 1);
                    ~applyVoiceLevels.(vid);
                    ~scheduleReset.(3.0, {
                        state[\register] = (state[\register] - 1).clip(0, ~registerBright.size - 1);
                        ~applyVoiceLevels.(vid);
                    });
                };
                ~scheduleReset.(2.5, {
                    ~globalAmpBoost = 1.0;
                    ~applyAllVoices.();
                });
            },
            stillness: { |strength|
                ~globalAmpBoost = (0.6 + (1.0 - strength) * 0.2).clip(0.4, 1.0);
                ~voiceState.keysValuesDo { |vid, state|
                    state[\trem] = state[\trem] * 0.2;
                    state[\register] = state[\register].min(1);
                    ~applyVoiceLevels.(vid);
                    ~scheduleReset.(4.0, {
                        state[\register] = (state[\register] + 1).clip(0, ~registerBright.size - 1);
                        ~applyVoiceLevels.(vid);
                    });
                };
                ~scheduleReset.(5.0, {
                    ~globalAmpBoost = 1.0;
                    ~applyAllVoices.();
                });
            }
        ]
    );

    ~runGestureHandler = { |scope, type, args|
        var handlers = ~gestureHandlers[scope];
        var handler = handlers[type.asSymbol];
        if (handler.notNil) {
            handler.valueArray(args);
        } {
            ("[gesture] no handler for % (%)".format(scope, type)).postln;
        };
    };

    OSCdef(\crowdGestureVoice, { |msg|
        var vid = msg[1].asInteger;
        var type = msg[2].asString;
        var strength = msg[3].asFloat.clip(0, 1);
        var extra = msg[4].asFloat;
        ~runGestureHandler.(\voice, type, [vid, strength, extra]);
    }, '/room/gesture/voice');

    OSCdef(\crowdGestureZone, { |msg|
        var camId = msg[1].asInteger;
        var type = msg[2].asString;
        var strength = msg[3].asFloat.clip(0, 1);
        var zoneIndex = if (msg.size > 4) { msg[4].asInteger } { -1 };
        if (type.beginsWith("sweep")) {
            ~runGestureHandler.(\zone, \sweep, [camId, type, strength]);
        } {
            ~runGestureHandler.(\zone, type, [camId, strength, zoneIndex]);
        };
    }, '/room/gesture/zone');

    OSCdef(\crowdGestureGlobal, { |msg|
        var type = msg[1].asString;
        var strength = msg[2].asFloat.clip(0, 1);
        ~runGestureHandler.(\global, type, [strength]);
    }, '/room/gesture/global');

    "Crowd Organ OSC listeners up on port %\n".postf(NetAddr.langPort);
    "Gesture handlers live in ~gestureHandlers – tweak them on the fly.".postln;
};
