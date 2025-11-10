import oscP5.*;
import netP5.*;
import java.util.*;

// This Processing sketch functions as a live oscilloscope for the gesture
// system. We keep the code intentionally narrative: every draw step explains
// what metric is being visualized so operators can tweak mappings mid-show.

OscP5 osc;

int NUM_VOICES = 8;
int CAMS = 2;

class Voice {
  boolean active = false;
  float x, y, z;
  float size;
  float motion;
  float energy;
  float note;
  float velocity;
  String lastGestureType = "";
  float lastGestureStrength = 0.0;
  int lastGestureFrame = -999;
}

class ZoneFlash {
  int camId;
  String type;
  float strength;
  int zoneIndex; // -1 for sweeps
  int startFrame;
  int lifespan = 180;
}

class GestureLogEntry {
  String scope;
  String label;
  String type;
  float strength;
  String detail;
  int frame;
}

Voice[] voices = new Voice[NUM_VOICES];

float globalMotion = 0.0;

int[] camCols = new int[CAMS];
int[] camRows = new int[CAMS];
float[][] camZones = new float[CAMS][];  // each is length cols*rows

ArrayList<ZoneFlash> zoneFlashes = new ArrayList<ZoneFlash>();
ArrayList<GestureLogEntry> gestureLog = new ArrayList<GestureLogEntry>();
int maxGestureLog = 18;

boolean showVoiceGestures = true;
boolean showZoneGestures = true;
boolean showGlobalGestures = true;

String lastGlobalGestureType = "";
float lastGlobalGestureStrength = 0.0;
int lastGlobalGestureFrame = -999;

void setup() {
  size(920, 720);
  frameRate(60);
  osc = new OscP5(this, 9000); // listen on port 9000

  for (int i = 0; i < NUM_VOICES; i++) {
    voices[i] = new Voice();
  }

  textAlign(CENTER, CENTER);
  textSize(12);
}

void draw() {
  cleanupZoneFlashes();

  background(8);

  fill(255);
  textAlign(CENTER, CENTER);
  text("CrowdOrganDashboard - OSC monitor", width/2, 20);

  drawGlobalMeter();
  drawVoices();
  drawCameraGrids();
  drawGestureLog(width - 240, 120, 220, 260);
  drawFooter();
}

void drawGlobalMeter() {
  // A quick glance widget for how rowdy the room is overall.
  float gmWidth = 220;
  float gmHeight = 12;
  float gmX = width - gmWidth - 20;
  float gmY = 36;

  noFill();
  stroke(90);
  rect(gmX, gmY, gmWidth, gmHeight);

  noStroke();
  fill(0, 200, 255);
  rect(gmX, gmY, gmWidth * constrain(globalMotion, 0, 1), gmHeight);

  fill(255);
  textAlign(LEFT, CENTER);
  text("Global motion: " + nf(globalMotion, 1, 2), gmX, gmY - 12);

  if (showGlobalGestures && frameCount - lastGlobalGestureFrame < 240) {
    float age = (float)(frameCount - lastGlobalGestureFrame) / 240.0;
    int col = color(255, 0, 170, (1.0 - age) * 220);
    fill(col);
    textAlign(LEFT, CENTER);
    text("Last global gesture: " + lastGlobalGestureType + " (" + nf(lastGlobalGestureStrength, 1, 2) + ")", gmX, gmY + gmHeight + 14);
  }
}

void drawVoices() {
  textAlign(CENTER, CENTER);
  pushMatrix();
  translate(width/2 - 80, height/2);

  for (int i = 0; i < NUM_VOICES; i++) {
    Voice v = voices[i];
    if (!v.active) continue;

    // Convert normalized coordinates to a simple stage map.
    float px = v.x * 320.0;
    float py = (1.0 - v.y) * 320.0;
    float radius = map(v.size, 0.0, 1.0, 12.0, 70.0);

    int baseCol = color(
      100 + 155 * v.energy,
      150,
      255
    );

    noStroke();
    fill(baseCol);
    ellipse(px, py, radius*2, radius*2);

    fill(255);
    text(i + " n:" + nf(v.note, 1, 1), px, py);

    if (showVoiceGestures && frameCount - v.lastGestureFrame < 180) {
      float age = (float)(frameCount - v.lastGestureFrame) / 180.0;
      float fade = constrain(1.0 - age, 0.0, 1.0);
      stroke(255, 140, 0, 200 * fade);
      strokeWeight(3);
      noFill();
      // The ring size scales with strength so you can eyeball intensity.
      float ring = radius*2 + 40 * v.lastGestureStrength;
      ellipse(px, py, ring, ring);
      strokeWeight(1);
      fill(255, 220);
      text(v.lastGestureType + " " + nf(v.lastGestureStrength, 1, 2), px, py - radius - 16);
    }
  }
  popMatrix();
}

void drawCameraGrids() {
  float gridWidth = width / (float)CAMS;
  float gridHeight = 200;

  for (int camId = 0; camId < CAMS; camId++) {
    if (camZones[camId] == null) continue;

    int cols = camCols[camId];
    int rows = camRows[camId];
    if (cols <= 0 || rows <= 0) continue;

    float cellW = gridWidth / cols;
    float cellH = gridHeight / rows;

    float baseX = camId * gridWidth;
    float baseY = height - gridHeight - 60;

    fill(255);
    textAlign(LEFT, BOTTOM);
    text("Cam " + camId, baseX + 6, baseY - 6);

    ArrayList<ZoneFlash> pulses = new ArrayList<ZoneFlash>();
    ZoneFlash latestSweep = null;
    if (showZoneGestures) {
      for (ZoneFlash flash : zoneFlashes) {
        if (flash.camId != camId) continue;
        float age = frameCount - flash.startFrame;
        if (age > flash.lifespan) continue;
        if (flash.zoneIndex >= 0) {
          pulses.add(flash);
        } else if (latestSweep == null || flash.startFrame > latestSweep.startFrame) {
          latestSweep = flash;
        }
      }
    }

    int idx = 0;
    noStroke();
    for (int ry = 0; ry < rows; ry++) {
      for (int cx = 0; cx < cols; cx++) {
        float val = camZones[camId][idx];

        float x0 = baseX + cx * cellW;
        float y0 = baseY + ry * cellH;

        int col = color(0, 130 + 125 * val, 255 * val);
        fill(col);
        rect(x0, y0, cellW, cellH);

        if (showZoneGestures) {
          for (ZoneFlash flash : pulses) {
            if (flash.zoneIndex == idx) {
              float age = frameCount - flash.startFrame;
              float fade = constrain(1.0 - age / (float)flash.lifespan, 0.0, 1.0);
              fill(255, 150, 0, 160 * fade);
              rect(x0, y0, cellW, cellH);
              fill(0);
              textAlign(CENTER, CENTER);
              text(nf(flash.strength, 1, 2), x0 + cellW/2, y0 + cellH/2);
            }
          }
        }

        idx++;
      }
    }

    if (showZoneGestures && latestSweep != null) {
      float age = frameCount - latestSweep.startFrame;
      float fade = constrain(1.0 - age / (float)latestSweep.lifespan, 0.0, 1.0);
      fill(255, 180, 0, 200 * fade);
      textAlign(LEFT, TOP);
      text("Sweep: " + latestSweep.type + " (" + nf(latestSweep.strength, 1, 2) + ")", baseX + 6, baseY + gridHeight + 6);
    }
  }
}

void drawGestureLog(float x, float y, float w, float h) {
  if (gestureLog.isEmpty()) {
    return;
  }

  fill(0, 160);
  noStroke();
  rect(x, y, w, h);

  // The log doubles as a textual teaching aid: scope + label + type.
  textAlign(LEFT, TOP);
  fill(255);
  text("Gestures", x + 10, y + 8);

  float lineY = y + 28;
  float lineH = 16;
  for (int i = 0; i < gestureLog.size(); i++) {
    if (lineY > y + h - lineH) break;
    GestureLogEntry entry = gestureLog.get(i);
    float age = frameCount - entry.frame;
    float alpha = constrain(255 - age * 2, 80, 255);

    int scopeColor;
    if (entry.scope.equals("voice")) {
      scopeColor = color(0, 200, 255, alpha);
    } else if (entry.scope.equals("zone")) {
      scopeColor = color(255, 150, 0, alpha);
    } else if (entry.scope.equals("global")) {
      scopeColor = color(255, 0, 170, alpha);
    } else {
      scopeColor = color(200, alpha);
    }

    fill(scopeColor);
    String detail = entry.detail.length() > 0 ? (" " + entry.detail) : "";
    String label = "[" + entry.scope.charAt(0) + "] " + entry.label + " → " + entry.type + " " + nf(entry.strength, 1, 2) + detail;
    text(label, x + 10, lineY);
    lineY += lineH;
  }
}

void drawFooter() {
  textAlign(LEFT, BOTTOM);
  fill(180);
  text("toggle gestures: [v] voice  [z] zone  [g] global", 20, height - 18);

  String muted = "";
  if (!showVoiceGestures) muted += "voice muted  ";
  if (!showZoneGestures) muted += "zone muted  ";
  if (!showGlobalGestures) muted += "global muted";
  if (muted.length() > 0) {
    fill(255, 80, 80);
    text(muted, 20, height - 36);
  }
}

void cleanupZoneFlashes() {
  for (int i = zoneFlashes.size() - 1; i >= 0; i--) {
    ZoneFlash flash = zoneFlashes.get(i);
    if (frameCount - flash.startFrame > flash.lifespan) {
      zoneFlashes.remove(i);
    }
  }
}

void addZoneFlash(int camId, String type, float strength, int zoneIndex) {
  ZoneFlash flash = new ZoneFlash();
  flash.camId = camId;
  flash.type = type;
  flash.strength = strength;
  flash.zoneIndex = zoneIndex;
  flash.startFrame = frameCount;
  if (type.startsWith("pulse")) {
    flash.lifespan = 150;
  } else {
    flash.lifespan = 200;
  }
  zoneFlashes.add(flash);
}

void pushGestureLog(String scope, String label, String type, float strength, String detail) {
  GestureLogEntry entry = new GestureLogEntry();
  entry.scope = scope;
  entry.label = label;
  entry.type = type;
  entry.strength = strength;
  entry.detail = detail;
  entry.frame = frameCount;
  gestureLog.add(0, entry);
  while (gestureLog.size() > maxGestureLog) {
    gestureLog.remove(gestureLog.size() - 1);
  }
}

void keyPressed() {
  if (key == 'v' || key == 'V') {
    showVoiceGestures = !showVoiceGestures;
  } else if (key == 'z' || key == 'Z') {
    showZoneGestures = !showZoneGestures;
  } else if (key == 'g' || key == 'G') {
    showGlobalGestures = !showGlobalGestures;
  }
}

void oscEvent(OscMessage msg) {
  String addr = msg.addrPattern();

  if (addr.equals("/room/voice/active")) {
    int vid = msg.get(0).intValue();
    int activeFlag = msg.get(1).intValue();
    if (vid >= 0 && vid < NUM_VOICES) {
      voices[vid].active = (activeFlag == 1);
    }

  } else if (addr.equals("/room/voice/state")) {
    int vid = msg.get(0).intValue();
    if (vid >= 0 && vid < NUM_VOICES) {
      Voice v = voices[vid];
      v.x      = msg.get(1).floatValue();
      v.y      = msg.get(2).floatValue();
      v.z      = msg.get(3).floatValue();
      v.size   = msg.get(4).floatValue();
      v.motion = msg.get(5).floatValue();
      v.energy = msg.get(6).floatValue();
    }

  } else if (addr.equals("/room/voice/note")) {
    int vid = msg.get(0).intValue();
    if (vid >= 0 && vid < NUM_VOICES) {
      Voice v = voices[vid];
      v.note     = msg.get(1).floatValue();
      v.velocity = msg.get(2).floatValue();
    }

  } else if (addr.equals("/room/global/motion")) {
    globalMotion = msg.get(0).floatValue();

  } else if (addr.equals("/room/camera/zones")) {
    int camId = msg.get(0).intValue();
    int cols  = msg.get(1).intValue();
    int rows  = msg.get(2).intValue();

    if (camId >= 0 && camId < CAMS && cols > 0 && rows > 0) {
      int numZones = cols * rows;
      if (msg.arguments().length >= 3 + numZones) {
        camCols[camId] = cols;
        camRows[camId] = rows;

        if (camZones[camId] == null || camZones[camId].length != numZones) {
          camZones[camId] = new float[numZones];
        }

        for (int i = 0; i < numZones; i++) {
          camZones[camId][i] = msg.get(3 + i).floatValue();
        }
      }
    }

  } else if (addr.equals("/room/gesture/voice")) {
    int vid = msg.get(0).intValue();
    String type = msg.get(1).stringValue();
    float strength = msg.get(2).floatValue();
    float extra = msg.get(3).floatValue();
    handleVoiceGesture(vid, type, strength, extra);

  } else if (addr.equals("/room/gesture/zone")) {
    int camId = msg.get(0).intValue();
    String type = msg.get(1).stringValue();
    float strength = msg.get(2).floatValue();
    int zoneIndex = (msg.arguments().length >= 4) ? msg.get(3).intValue() : -1;
    handleZoneGesture(camId, type, strength, zoneIndex);

  } else if (addr.equals("/room/gesture/global")) {
    String type = msg.get(0).stringValue();
    float strength = msg.get(1).floatValue();
    handleGlobalGesture(type, strength);
  }
}

void handleVoiceGesture(int voiceId, String type, float strength, float extra) {
  if (voiceId >= 0 && voiceId < NUM_VOICES) {
    Voice v = voices[voiceId];
    v.lastGestureType = type;
    v.lastGestureStrength = strength;
    v.lastGestureFrame = frameCount;
  }

  // Extra carries additional context such as y-position or hold duration.
  String detail = "";
  if (type.equals("raise") || type.equals("lower")) {
    detail = "@y " + nf(extra, 1, 2);
  } else if (type.equals("hold")) {
    detail = "len " + nf(extra, 1, 2);
  }
  pushGestureLog("voice", "voice " + voiceId, type, strength, detail);
}

void handleZoneGesture(int camId, String type, float strength, int zoneIndex) {
  addZoneFlash(camId, type, strength, zoneIndex);
  String detail = (zoneIndex >= 0) ? ("zone " + zoneIndex) : "";
  pushGestureLog("zone", "cam " + camId, type, strength, detail);
}

void handleGlobalGesture(String type, float strength) {
  lastGlobalGestureType = type;
  lastGlobalGestureStrength = strength;
  lastGlobalGestureFrame = frameCount;
  pushGestureLog("global", "room", type, strength, "");
}
